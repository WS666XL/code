#include "filetcpmgr.h"
#include "Logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QCryptographicHash>
#include "usermgr.h"

FileTcpMgr::~FileTcpMgr()
{

}

// 私有构造：单例只在第一次 GetInstance() 时创建，
// 所有 socket 信号和内部转发信号都在这里 connect 好
FileTcpMgr::FileTcpMgr(QObject* parent)
    : QObject(parent), _host(""), _port(0),
      _b_recv_pending(false), _message_id(0), _message_len(0)
{
    // 连接建立：通知外界"资源服务器链路可用"，可以开始发上传请求了
    QObject::connect(&_socket, &QTcpSocket::connected, [this]() {
        LLOG_DEBUG << "FileTcpMgr connected to resource server!";
        // 重连后旧的窗口/排队状态全部作废：旧连接上的在途分块不会再有回执，
        // 不清零的话 _cwnd_size 会永久占满，后续上传全部被卡住
        _cwnd_size = 0;
        _active_upload.clear();
        _pending_uploads.clear();
        _simple_uploads.clear();
        _downloading.clear();
        emit sig_res_connect(true);
    });

    QObject::connect(&_socket, &QTcpSocket::readyRead, [this]() {
        // 先把这次到达的数据全部追加进缓冲区，再用循环从缓冲区里"切"完整包：
        // TCP 是字节流没有消息边界，一次 readyRead 可能只有半条消息（半包），
        // 也可能粘着好几条消息（粘包），所以必须循环解析，
        // 直到缓冲区剩下的数据不够拼出一条完整包为止
        _buffer.append(_socket.readAll());

        forever {
            // ---- 第一步：解析 6 字节包头（2B 消息ID + 4B 包体长度）----
            if (!_b_recv_pending) {
                // 连包头都不够，等下一次 readyRead 再拼
                if (_buffer.size() < FILE_UPLOAD_HEAD_LEN) {
                    return;
                }
                // 每轮循环新建流按值读包头，避免 TcpMgr 那种"流挂在缓冲区指针上、
                // 缓冲区被 mid 截断后位置对不上"的隐患
                QDataStream stream(_buffer);
                stream.setByteOrder(QDataStream::BigEndian); // 协议约定大端
                // 长度字段占 4 字节，必须按 quint32 读：
                // 若按 quint16 读只会吞掉一半，剩下 2 字节会被误当成包体，
                // 之后整条流的边界就全错了
                stream >> _message_id >> _message_len;
                // 从缓冲区头部移除已解析的 6 字节
                _buffer.remove(0, FILE_UPLOAD_HEAD_LEN);
            }

            // ---- 第二步：等包体收全 ----
            // 剩余数据不够包体长度：挂起标记置位，退出等下一次数据到了接着算。
            // 没收全的部分已经躺在 _buffer 里，不会丢
            if (_buffer.size() < qint64(_message_len)) {
                _b_recv_pending = true;
                return;
            }
            _b_recv_pending = false;

            // ---- 第三步：切出完整包体，按消息 ID 分发 ----
            QByteArray body = _buffer.left(_message_len);
            _buffer.remove(0, _message_len);
            handleMsg(_message_id, static_cast<int>(_message_len), body);
            // 回到 forever 继续尝试解析下一条：这就是粘包场景下一次读完多条消息的关键
        }
    });

    // Qt 5.15+ 的统一错误信号：连接被拒、找不到主机、断线等都在这里先打日志
    QObject::connect(&_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
        [this](QAbstractSocket::SocketError socketError) {
        LLOG_DEBUG << "FileTcpMgr socket error:" << _socket.errorString();
        // 只把"根本没连上"的致命错误当连接失败上报（sig_res_connect(false)），
        // 断线场景由下面的 disconnected 信号统一处理，避免重复通知
        if (socketError == QAbstractSocket::ConnectionRefusedError ||
            socketError == QAbstractSocket::HostNotFoundError) {
            emit sig_res_connect(false);
        }
    });

    // 断开：资源服务器主动关连接或网络异常都会走到这里
    QObject::connect(&_socket, &QTcpSocket::disconnected, [this]() {
        LLOG_DEBUG << "FileTcpMgr disconnected from resource server.";
        // 断线后必须清掉残留的半包状态，否则下次重连会把旧数据当新包头解析
        _buffer.clear();
        _b_recv_pending = false;
        // 上传窗口同理清零：在途分块已不可能收到回执
        _cwnd_size = 0;
        _active_upload.clear();
        _pending_uploads.clear();
        _simple_uploads.clear();
        // 下载上下文也清空：本地 .tmp 保留，下次点下载会按 .tmp 大小从断点续传
        _downloading.clear();
        emit sig_connection_closed();
    });

    // 发送走"信号转槽"：外部只 emit sig_send_data，真正的 write 在本对象所属线程执行，
    // 即使将来 FileTcpMgr 被 moveToThread，也不会出现跨线程直接操作 socket 的问题
    QObject::connect(this, &FileTcpMgr::sig_send_data, this, &FileTcpMgr::slot_send_data);

    initHandlers();
}

// 注册"消息ID → 处理函数"分发表：readyRead 里解出完整包后按 id 查这里
void FileTcpMgr::initHandlers()
{
    // 1002 链路自测回包：服务器把请求里的 data 原样带回，
    // 收到即说明"客户端 ↔ 资源服务器"整条链路（连接/组包/解包/base64）都是通的
    _handlers.insert(RES_ID_TEST_MSG_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        LLOG_DEBUG << "FileTcpMgr handle id is " << id;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            LLOG_DEBUG << "FileTcpMgr test rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            LLOG_DEBUG << "FileTcpMgr test rsp missing error field";
            return;
        }
        if (jsonObj["error"].toInt() != ErrorCodes::SUCCESS) {
            LLOG_DEBUG << "FileTcpMgr test rsp error is " << jsonObj["error"].toInt();
            return;
        }
        // 回显内容通过信号抛出去；当前阶段没有界面接收，收到只打日志
        emit sig_test_rsp(jsonObj["data"].toString());
    });

    // 1004 上传进度回执：服务器每落盘一块就回一条，格式
    // {error, total_size, seq, name, trans_size, last}
    _handlers.insert(RES_ID_UPLOAD_FILE_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        LLOG_DEBUG << "FileTcpMgr handle id is " << id;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            LLOG_DEBUG << "FileTcpMgr upload rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            LLOG_DEBUG << "FileTcpMgr upload rsp missing error field";
            return;
        }
        int err = jsonObj["error"].toInt();
        QString md5 = jsonObj["md5"].toString();
        QString name = jsonObj["name"].toString();
        if (err != ErrorCodes::SUCCESS) {
            LLOG_DEBUG << "FileTcpMgr upload rsp error is " << err;
            // 服务端进度丢了（Redis 过期/重启）：已经确认过至少一块的话，
            // 自动回到第 1 块重传（服务端 seq==1 会 truncate 旧 .part，不会拼坏）
            auto iter = _simple_uploads.find(md5);
            if (err == RES_FILE_NOT_EXISTS && iter != _simple_uploads.end()
                && iter->last_confirmed_seq > 0) {
                iter->next_seq = 1;
                iter->last_confirmed_seq = 0;
                SendNextSimpleChunk(md5);
            }
            return;
        }

        int seq = jsonObj["seq"].toInt();
        int trans_size = jsonObj["trans_size"].toInt();  // 服务器已收到的字节数
        int total_size = jsonObj["total_size"].toInt();  // 文件总大小
        int last = jsonObj["last"].toInt();              // 1 表示服务器已收完最后一块

        // 进度信号：界面拿它刷进度条（trans_size/total_size 算百分比）
        emit sig_upload_progress(trans_size, total_size);

        auto iter = _simple_uploads.find(md5);
        if (iter == _simple_uploads.end()) {
            // 已经收尾（重复回执/晚到回执）：只按 last 补发完成信号
            if (last == 1) {
                emit sig_upload_done(name);
            }
            return;
        }

        iter->last_confirmed_seq = seq;
        if (last == 1 || seq >= iter->max_seq) {
            LLOG_DEBUG << "FileTcpMgr upload done:" << name;
            _simple_uploads.erase(iter);
            emit sig_upload_done(name);
            return;
        }

        // 回执驱动：收到一块的确认才发下一块（day38 断点续传的基础）
        SendNextSimpleChunk(md5);
    });

    // 1006 同步上传进度回包（day38 断点续传）：点"继续"后服务器告诉我们
    // 已经确认到第几块/多少字节，客户端从断点继续，而不是从头再传。
    // {error, md5, name, seq, trans_size, total_size}
    _handlers.insert(RES_ID_SYNC_FILE_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            LLOG_DEBUG << "FileTcpMgr sync file rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            LLOG_DEBUG << "FileTcpMgr sync file rsp missing error field";
            return;
        }

        int err = jsonObj["error"].toInt();
        QString name = jsonObj["name"].toString();
        qint64 seq = jsonObj["seq"].toInt();
        qint64 trans_size = jsonObj["trans_size"].toInt();
        qint64 total_size = jsonObj["total_size"].toInt();

        auto msg_info = UserMgr::GetInstance()->GetTransFileByName(name);
        if (!msg_info) {
            LLOG_DEBUG << "FileTcpMgr sync file rsp: trans file not found:" << name;
            return;
        }

        if (err == RES_FILE_NOT_EXISTS) {
            // 服务器没有进度（Redis 过期/上次已传完并清理）：从第 1 块重传。
            // 服务端收到 seq==1 会 truncate 掉可能残留的 .part，所以不会拼坏文件
            LLOG_DEBUG << "FileTcpMgr sync file: no progress, restart upload" << name;
            msg_info->_seq = 0;
            msg_info->_last_confirmed_seq = 0;
            msg_info->_rsp_size = 0;
            msg_info->_flighting_seqs.clear();
            msg_info->_rsp_seqs.clear();
            _cwnd_size = 0;
        }
        else if (err != RES_SUCCESS) {
            LLOG_DEBUG << "FileTcpMgr sync file rsp error:" << err << name;
            msg_info->_transfer_state = TransferState::Failed;
            emit sig_chat_upload_failed(name);
            return;
        }
        else {
            // 服务器已确认到 seq：把已发游标拨回去，BatchSend 会 seek 到 seq*32KB 继续发。
            // 清空在途/窗口记录：续传后以服务器进度为准，旧连接的迟到回执不再放行窗口
            LLOG_DEBUG << "FileTcpMgr sync file resume:" << name
                     << "seq" << seq << "trans" << trans_size << "/" << total_size;
            msg_info->_seq = qMax<qint64>(0, seq);
            msg_info->_last_confirmed_seq = static_cast<int>(seq);
            msg_info->_rsp_size = trans_size;
            if (total_size > 0) {
                msg_info->_total_size = total_size;
                msg_info->_max_seq = (total_size + MAX_FILE_LEN - 1) / MAX_FILE_LEN;
            }
            msg_info->_flighting_seqs.clear();
            msg_info->_rsp_seqs.clear();
            _cwnd_size = 0;

            // 暂停期间服务器其实已经收完（在途块全部确认）：直接按完成收尾
            if (trans_size >= msg_info->_total_size
                || (msg_info->_max_seq > 0 && seq >= msg_info->_max_seq)) {
                msg_info->_rsp_size = msg_info->_total_size;
                emit sig_chat_upload_progress(name, msg_info->_total_size, msg_info->_total_size);
                FinishChatUpload(msg_info, true);
                return;
            }
        }

        msg_info->_transfer_state = TransferState::Uploading;
        msg_info->_transfer_type = TransferType::Upload;
        emit sig_chat_upload_progress(name, msg_info->_rsp_size, msg_info->_total_size);
        BatchSend(msg_info);
    });

    // 1032 头像上传回包：服务器每落盘一块就回一条
    // {error, total_size, seq, name, trans_size, last, md5, uid, last_seq}。
    // 与 1004 的差别：1031 是"落盘后才回"，且最后一块的回包发在 DB/Redis 更新之后，
    // 所以收到 trans_size==total_size 的 1032 就代表"头像已完整入库"，而不是仅仅收完
    _handlers.insert(RES_ID_UPLOAD_HEAD_ICON_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        LLOG_DEBUG << "FileTcpMgr handle id is " << id;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            LLOG_DEBUG << "FileTcpMgr head icon rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            LLOG_DEBUG << "FileTcpMgr head icon rsp missing error field";
            return;
        }
        QString md5 = jsonObj["md5"].toString();
        QString name = jsonObj["name"].toString();
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            // 服务端进度丢了但本地已确认过块：自动从第 1 块重传
            auto iter = _simple_uploads.find(md5);
            if (err == RES_FILE_NOT_EXISTS && iter != _simple_uploads.end()
                && iter->last_confirmed_seq > 0) {
                iter->next_seq = 1;
                iter->last_confirmed_seq = 0;
                SendNextSimpleChunk(md5);
                return;
            }

            // 常见错误：1010 token 失效 / 1011 uid 无效（Redis 查不到 token）——
            // 一般是登录态过期后仍尝试上传，提示用户重新登录即可
            LLOG_DEBUG << "FileTcpMgr head icon upload failed, err is" << err;
            _simple_uploads.remove(md5);
            emit sig_upload_head_failed(err);
            return;
        }

        int seq = jsonObj["seq"].toInt();
        int trans_size = jsonObj["trans_size"].toInt();
        int total_size = jsonObj["total_size"].toInt();
        emit sig_upload_progress(trans_size, total_size);

        auto iter = _simple_uploads.find(md5);
        if (iter == _simple_uploads.end()) {
            // 已经收尾的晚到回执：只补完成信号
            if (trans_size == total_size) {
                emit sig_upload_head_done(name);
            }
            return;
        }

        iter->last_confirmed_seq = seq;
        if (seq >= iter->max_seq || trans_size == total_size) {
            LLOG_DEBUG << "FileTcpMgr head icon upload done:" << name
                     << "total_size" << total_size;
            _simple_uploads.erase(iter);
            emit sig_upload_head_done(name);
            return;
        }

        // 回执驱动：下一块落盘确认后才发下一块（断点续传基础）
        SendNextSimpleChunk(md5);
    });

    // 1038 图片/文件上传回执：{error, total_size, seq, name, trans_size, last,
    // message_id, sender, receiver}，每块到达服务器后回一条（与 1004 同构）。
    // day40：这条回执同时是拥塞窗口的"放行信号"——收到一块就让出一格窗口，
    // 再把文件往后补一块，形成"发送 → 回执 → 再发送"的滑动窗口。
    // 最后一块回包到达只代表"服务器已收到最后一块"，DB 状态更新和对方通知
    // 在服务端 FileWorker 线程里紧随其后异步完成
    _handlers.insert(RES_ID_IMG_CHAT_UPLOAD_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            LLOG_DEBUG << "FileTcpMgr img chat upload rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            LLOG_DEBUG << "FileTcpMgr img chat upload rsp missing error field";
            return;
        }
        int err = jsonObj["error"].toInt();
        QString name = jsonObj["name"].toString();
        auto msg_info = UserMgr::GetInstance()->GetTransFileByName(name);

        if (err != ErrorCodes::SUCCESS) {
            LLOG_DEBUG << "FileTcpMgr img chat upload failed, err is" << err
                     << "name is" << name;
            int err_seq = jsonObj["seq"].toInt();
            // 服务端进度丢了（Redis 过期/重启）：从第 1 块自动重传一次。
            // seq==1 还报 FileNotExists 说明不是进度丢失，走失败分支
            if (err == RES_FILE_NOT_EXISTS && msg_info && err_seq > 1) {
                msg_info->_seq = 0;
                msg_info->_last_confirmed_seq = 0;
                msg_info->_rsp_size = 0;
                msg_info->_flighting_seqs.clear();
                msg_info->_rsp_seqs.clear();
                _cwnd_size = 0;
                msg_info->_transfer_state = TransferState::Uploading;
                emit sig_chat_upload_progress(name, 0, msg_info->_total_size);
                BatchSend(msg_info);
                return;
            }

            // 首块 token 校验失败/服务端进度异常：释放窗口并通知界面切失败态，
            // 否则这些 seq 永远等不到回执，窗口会一直卡死，后续上传全发不出去
            if (msg_info) {
                FinishChatUpload(msg_info, false);
            }
            else if (_active_upload == name) {
                // 上传任务已经收尾（重复/晚到错误回执）：窗口也要让给下一个文件
                _active_upload.clear();
                StartNextUpload();
            }
            return;
        }

        bool instant = jsonObj["instant"].toBool();
        if (!msg_info) {
            // 已经收尾的文件（例如最后一块回执晚到）：只释放窗口，不再续发。
            // 秒传的晚到回执不占窗口（FinishChatUpload 已经一次性收回）
            if (!instant && _cwnd_size > 0) {
                --_cwnd_size;
            }
            return;
        }

        if (instant) {
            // 秒传命中：服务端已有相同 md5 的文件，不用再发剩余分块
            LLOG_DEBUG << "FileTcpMgr img chat upload instant done:" << name;
            msg_info->_rsp_size = msg_info->_total_size;
            emit sig_chat_upload_progress(name, msg_info->_total_size, msg_info->_total_size);
            FinishChatUpload(msg_info, true);
            return;
        }

        int seq = jsonObj["seq"].toInt();
        // 一块回执放行一格窗口。只有确实在途的 seq 才回收，
        // 重复回执/脏回执不能把窗口白送出去
        if (msg_info->_flighting_seqs.remove(seq) && _cwnd_size > 0) {
            --_cwnd_size;
        }
        msg_info->_rsp_seqs.insert(seq);
        // 按序推进"连续确认"游标：万一回执乱序，进度也不会跳着涨
        while (msg_info->_rsp_seqs.contains(msg_info->_last_confirmed_seq + 1)) {
            ++msg_info->_last_confirmed_seq;
        }
        // 已发游标至少推进到服务器确认的位置：暂停后继续时不会把已确认的块再发一遍
        if (seq > msg_info->_seq) {
            msg_info->_seq = seq;
        }
        // 进度只统计"确认收到"的部分（已确认块数 × 块大小，末块按总大小封顶）
        msg_info->_rsp_size = qMin<qint64>(
            msg_info->_last_confirmed_seq * qint64(MAX_FILE_LEN), msg_info->_total_size);
        emit sig_chat_upload_progress(name, msg_info->_rsp_size, msg_info->_total_size);

        if (msg_info->_max_seq > 0 && msg_info->_last_confirmed_seq >= msg_info->_max_seq) {
            // 所有块都已确认：本次上传结束，把窗口让给排队中的下一个文件
            LLOG_DEBUG << "FileTcpMgr img chat upload done:" << name
                     << "blocks" << msg_info->_max_seq;
            msg_info->_rsp_size = msg_info->_total_size;
            emit sig_chat_upload_progress(name, msg_info->_total_size, msg_info->_total_size);
            FinishChatUpload(msg_info, true);
            return;
        }

        // 暂停状态：回执只用来更新进度，不再补发新块（恢复后走 1005 同步再续）
        if (msg_info->_transfer_state != TransferState::Uploading) {
            return;
        }

        // 窗口让出一格，继续补发下一块
        BatchSend(msg_info);
    });

    // 1034 下载回包：服务器按 (seq-1)*32KB 偏移读一块 base64 回来。
    // {error, data, seq, total_size, current_size, is_last, name, client_path}。
    // 客户端按 seq 覆盖/追加写本地文件；is_last=0 就带 seq+1 继续请求，
    // is_last=1 清掉下载上下文并发 sig_avatar_downloaded 通知界面刷新
    _handlers.insert(RES_ID_DOWNLOAD_FILE_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            LLOG_DEBUG << "FileTcpMgr download rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            LLOG_DEBUG << "FileTcpMgr download rsp missing error field";
            return;
        }
        int err = jsonObj["error"].toInt();
        QString name = jsonObj["name"].toString();
        //下载上下文按 client_path 找回：不同用户的头像同名（都叫 head.png），
        //只按 name 查会把 A 的下载续到 B 的上下文里/互相覆盖
        QString ctx_key = jsonObj["client_path"].toString();
        int seq = jsonObj["seq"].toInt();

        auto ctx_iter = _downloading.find(ctx_key);
        if (err != ErrorCodes::SUCCESS) {
            // 常见错误：1012 文件不存在、1017 序号接不上（服务器进度丢了/对不上）、
            // 1010/1011 登录态失效
            LLOG_DEBUG << "FileTcpMgr download failed, err is" << err
                     << "name is" << name << "seq" << seq;

            // 断点续传失败回退：聊天图片/文件（唯一文件名）本地 .tmp 还有半截，
            // 但服务器进度已过期/序号接不上——删掉 .tmp 从第 1 块重下一次，
            // 只重试一次，避免「续传失败→重下→再失败」死循环
            if (ctx_iter != _downloading.end() && seq > 1
                && ctx_iter->resume_allowed && !ctx_iter->resume_tried) {
                LLOG_DEBUG << "FileTcpMgr download resume failed, restart from 1:" << name;
                QFile::remove(ctx_iter->client_path + ".tmp");
                ctx_iter->next_seq = 1;
                ctx_iter->in_flight = true;
                ctx_iter->resume_tried = true;
                SendDownloadReq(name, ctx_iter->owner_uid, ctx_iter->client_path, 1);
                return;
            }

            if (ctx_iter != _downloading.end()) {
                _downloading.erase(ctx_iter);
            }
            //通知界面恢复可重试状态（文件卡片状态会卡在"下载中"，图片气泡占位不变）
            emit sig_download_failed(name);
            return;
        }

        if (ctx_iter == _downloading.end()) {
            LLOG_DEBUG << "FileTcpMgr download rsp for unknown file:" << name
                     << ctx_key;
            return;
        }
        ctx_iter->in_flight = false;

        bool is_last = jsonObj["is_last"].toBool();
        qint64 total_size = jsonObj["total_size"].toString().toLongLong();
        qint64 current_size = jsonObj["current_size"].toString().toLongLong();
        QByteArray decoded = QByteArray::fromBase64(
            jsonObj["data"].toString().toUtf8());

        // 写入临时文件、收完最后一块再原子 rename 成正式文件：
        // 同机多开客户端共享缓存目录，另一进程可能正在读这个文件——
        // 直接覆盖写会让读方读到半截图（加载失败 → 再触发下载 → 死循环）。
        // 断点续传时 seq>1：.tmp 里已有前 seq-1 块，用追加模式接着写
        QString write_path = ctx_iter->client_path + ".tmp";
        QIODevice::OpenMode mode = (seq == 1)
            ? QIODevice::WriteOnly
            : (QIODevice::WriteOnly | QIODevice::Append);
        QFile file(write_path);
        if (!file.open(mode)) {
            LLOG_DEBUG << "FileTcpMgr download open file failed:" << write_path
                     << file.errorString();
            _downloading.erase(ctx_iter);
            emit sig_download_failed(name);
            return;
        }
        file.write(decoded);
        file.close();

        // 记录下一块序号：暂停时停在这里，恢复后从这里接着请求
        ctx_iter->next_seq = seq + 1;
        emit sig_chat_download_progress(name, current_size, total_size);

        if (is_last) {
            // 聊天文件：临时文件是 UUID，最终要落到“下载/llfcchat/原始文件名”；
            // 图片/头像 final_path 为空，仍然就是原来的缓存路径
            QString target = ctx_iter->final_path.isEmpty()
                                 ? ctx_iter->client_path
                                 : ctx_iter->final_path;
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::remove(target);
            bool moved = QFile::rename(write_path, target);
            if (!moved) {
                // AppData 和下载目录可能不在同一个文件系统，rename 会失败，
                // 退回“复制 + 删除临时文件”
                if (QFile::copy(write_path, target)) {
                    QFile::remove(write_path);
                    moved = true;
                }
            }
            if (!moved) {
                LLOG_DEBUG << "FileTcpMgr download move failed:" << name
                         << "->" << target;
                _downloading.erase(ctx_iter);
                emit sig_download_failed(name);
                return;
            }

            QString downloaded_name = ctx_iter->name;
            bool remember = !ctx_iter->final_path.isEmpty();
            _downloading.erase(ctx_iter);
            if (remember) {
                // 记录 unique_name → 实际保存路径，重新登录后优先复用
                RememberSavedPath(downloaded_name, target);
            }
            LLOG_DEBUG << "FileTcpMgr download done:" << downloaded_name
                     << "->" << target;
            // 本地文件已就绪：通知界面把还显示着占位图的头像/图片气泡重新加载
            emit sig_file_downloaded(downloaded_name);
            return;
        }

        // 暂停：本块已写好但不再请求下一块（.tmp 保留，继续时接着 next_seq 请求）
        if (ctx_iter->paused) {
            return;
        }

        ctx_iter->in_flight = true;
        SendDownloadReq(name, ctx_iter->owner_uid,
                        ctx_iter->client_path, ctx_iter->next_seq);
    });
}

void FileTcpMgr::handleMsg(quint16 id, int len, QByteArray data)
{
    auto find_iter = _handlers.find(id);
    if (find_iter == _handlers.end()) {
        // 未知 ID 不处理：资源服务器后续加新消息类型时客户端没跟上也不会崩
        LLOG_DEBUG << "FileTcpMgr not found id [" << id << "] to handle";
        return;
    }
    find_iter.value()(id, len, data);
}

// 连接资源服务器：host 形如 "127.0.0.1"，port 一般 9090
// 结果通过 sig_res_connect(bool) 异步反馈（连接成功/失败都是事件驱动的）
void FileTcpMgr::ConnectResServer(const QString& host, quint16 port)
{
    LLOG_DEBUG << "FileTcpMgr connecting to resource server" << host << ":" << port;
    _host = host;
    _port = port;
    _socket.connectToHost(host, port);
}

// 对外发送入口：不直接 write，而是 emit 内部信号转给 slot_send_data，
// 好处是多线程安全（见构造函数里的 connect 注释）
void FileTcpMgr::sendMsg(quint16 id, QByteArray body)
{
    emit sig_send_data(id, body);
}

// 真正发送：组 6 字节包头（2B 消息id + 4B 包体长度，均大端）+ 包体一次写入
void FileTcpMgr::slot_send_data(quint16 id, QByteArray body)
{
    // 连接异常直接丢弃，避免往未连接的 socket 写数据
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        LLOG_DEBUG << "FileTcpMgr slot_send_data: not connected, drop msg id" << id;
        return;
    }

    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);  // 和服务器约定的大端一致
    out << id << static_cast<quint32>(body.size());  // 2B id + 4B 长度 = 6B 包头
    block.append(body);                        // 接上 JSON 包体

    _socket.write(block);
}

// 通用文件上传（1003）：改为"一次发一块 + 1004 回执驱动下一块"，
// 这样任何一块确认后都能停/续，断线后也能用 1005 从服务端进度续传。
// 具体分块组包在 StartSimpleUpload / SendNextSimpleChunk 里，与 1031 共用
bool FileTcpMgr::UploadFile(QString path)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        LLOG_DEBUG << "FileTcpMgr UploadFile failed: resource server not connected";
        return false;
    }
    return StartSimpleUpload(path, RES_ID_UPLOAD_FILE_REQ);
}

// 上传头像（1031）：与 UploadFile 共用回执驱动的分块逻辑，
// 差别是请求额外带 token/uid（服务端首块校验登录态），完成信号走 1032
bool FileTcpMgr::UploadHeadIcon(QString path)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        LLOG_DEBUG << "FileTcpMgr UploadHeadIcon failed: resource server not connected";
        return false;
    }
    return StartSimpleUpload(path, RES_ID_UPLOAD_HEAD_ICON_REQ);
}

// ===================== day38 断点续传：上传侧辅助函数 =====================

// 计算 md5/总大小/总块数，登记 SimpleUploadCtx 并发出第 1 块。
// 1003（通用文件）和 1031（头像）共用：两者都是小文件，不需要 1037 的并发窗口
bool FileTcpMgr::StartSimpleUpload(const QString& path, short req_id)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LLOG_DEBUG << "FileTcpMgr StartSimpleUpload could not open file:"
                 << file.errorString();
        return false;
    }

    // md5 是续传进度 key：服务端把"传到第几块"存在 file_upload_<uid>|<md5> 里
    QCryptographicHash hash(QCryptographicHash::Md5);
    qint64 originalPos = file.pos();
    if (!hash.addData(&file)) {
        LLOG_DEBUG << "FileTcpMgr StartSimpleUpload failed to read:" << path;
        file.close();
        return false;
    }
    QString file_md5 = hash.result().toHex();
    file.seek(originalPos);
    file.close();

    QFileInfo fileInfo(path);
    qint64 total_size = fileInfo.size();
    if (total_size <= 0) {
        LLOG_DEBUG << "FileTcpMgr StartSimpleUpload: empty file, skip";
        return false;
    }

    SimpleUploadCtx ctx;
    ctx.path = path;
    ctx.name = fileInfo.fileName();   //只发文件名，服务器按 uid/文件名落盘
    ctx.md5 = file_md5;
    ctx.total_size = total_size;
    ctx.max_seq = (total_size + MAX_FILE_LEN - 1) / MAX_FILE_LEN;
    ctx.next_seq = 1;
    ctx.last_confirmed_seq = 0;
    ctx.req_id = req_id;

    _simple_uploads[file_md5] = ctx;
    LLOG_DEBUG << "FileTcpMgr StartSimpleUpload:" << ctx.name
             << "total" << total_size << "blocks" << ctx.max_seq;
    SendNextSimpleChunk(file_md5);
    return true;
}

// 从 ctx.next_seq 读一块发出去。回执照 1004/1032 handler 回来后再次调用，
// 形成"确认一块 → 发下一块"的节奏；暂停时 handler 不再调用它，发送自然停住
void FileTcpMgr::SendNextSimpleChunk(const QString& md5)
{
    auto iter = _simple_uploads.find(md5);
    if (iter == _simple_uploads.end()) {
        return;
    }
    SimpleUploadCtx& ctx = iter.value();
    if (ctx.next_seq > ctx.max_seq) {
        return;
    }

    QFile file(ctx.path);
    if (!file.open(QIODevice::ReadOnly)) {
        LLOG_DEBUG << "FileTcpMgr SendNextSimpleChunk open failed:"
                 << ctx.path << file.errorString();
        _simple_uploads.erase(iter);
        return;
    }
    file.seek((ctx.next_seq - 1) * MAX_FILE_LEN);
    QByteArray buffer = file.read(MAX_FILE_LEN);
    file.close();
    if (buffer.isEmpty()) {
        _simple_uploads.erase(iter);
        return;
    }

    qint64 seq = ctx.next_seq;
    QJsonObject jsonObj;
    jsonObj["md5"] = ctx.md5;
    jsonObj["name"] = ctx.name;
    jsonObj["seq"] = static_cast<int>(seq);
    jsonObj["trans_size"] = static_cast<int>((seq - 1) * MAX_FILE_LEN + buffer.size());
    jsonObj["total_size"] = static_cast<int>(ctx.total_size);
    // 用序号判断最后一块：文件大小正好是 32KB 整数倍时最后一块也是满的
    jsonObj["last"] = (seq == ctx.max_seq) ? 1 : 0;
    jsonObj["last_seq"] = static_cast<int>(ctx.max_seq);
    jsonObj["data"] = QString::fromLatin1(buffer.toBase64());
    if (ctx.req_id == RES_ID_UPLOAD_HEAD_ICON_REQ) {
        // 头像首块要带身份字段，服务端用 Redis utoken_<uid> 校验登录态
        jsonObj["token"] = UserMgr::GetInstance()->GetToken();
        jsonObj["uid"] = UserMgr::GetInstance()->GetUid();
    }

    QJsonDocument doc(jsonObj);
    sendMsg(ctx.req_id, doc.toJson(QJsonDocument::Compact));
    ++ctx.next_seq;
}

// 暂停/继续：暂停只改状态，1038 回执到达后看到不是 Uploading 就不会再补发新块。
// 继续先发 1005 问服务器进度，1006 回来后从断点续传（比本地游标更可靠：
// 暂停期间已经发出去的在途块，服务器可能已经收下了）
void FileTcpMgr::PauseUpload(const QString& name)
{
    auto msg_info = UserMgr::GetInstance()->GetTransFileByName(name);
    if (!msg_info) {
        return;
    }
    msg_info->_transfer_state = TransferState::Paused;
    LLOG_DEBUG << "FileTcpMgr pause upload:" << name;
}

void FileTcpMgr::ResumeUpload(const QString& name)
{
    auto msg_info = UserMgr::GetInstance()->GetTransFileByName(name);
    if (!msg_info) {
        return;
    }

    msg_info->_transfer_state = TransferState::Uploading;
    msg_info->_transfer_type = TransferType::Upload;

    // 窗口被别的文件占着：先排队，等它传完由 StartNextUpload 拉起
    if (!_active_upload.isEmpty() && _active_upload != name) {
        if (!_pending_uploads.contains(name)) {
            _pending_uploads.enqueue(name);
        }
        return;
    }
    _active_upload = name;

    if (msg_info->_md5.isEmpty()) {
        // 老数据没有 md5：退化为按本地连续确认序号继续
        msg_info->_seq = msg_info->_last_confirmed_seq;
        BatchSend(msg_info);
        return;
    }

    LLOG_DEBUG << "FileTcpMgr resume upload, sync progress:" << name;
    SendSyncUploadReq(msg_info->_md5, name);
}

void FileTcpMgr::SendSyncUploadReq(const QString& md5, const QString& name)
{
    QJsonObject jsonObj;
    jsonObj["md5"] = md5;
    jsonObj["name"] = name;
    jsonObj["uid"] = UserMgr::GetInstance()->GetUid();
    jsonObj["token"] = UserMgr::GetInstance()->GetToken();
    QJsonDocument doc(jsonObj);
    sendMsg(RES_ID_SYNC_FILE_REQ, doc.toJson(QJsonDocument::Compact));
}

// 聊天资源上传收尾：成功置 Completed，失败置 Failed 并通知气泡；
// 两种情况都要把该文件占用的窗口释放掉，否则连接上的窗口会永久卡死
void FileTcpMgr::FinishChatUpload(std::shared_ptr<MsgInfo> msg_info, bool success)
{
    if (!msg_info) {
        return;
    }

    QString name = msg_info->_unique_name;
    msg_info->_transfer_type = TransferType::Upload;
    if (success) {
        msg_info->_transfer_state = TransferState::Completed;
        msg_info->_rsp_size = msg_info->_total_size;
    }
    else {
        msg_info->_transfer_state = TransferState::Failed;
        emit sig_chat_upload_failed(name);
    }

    _cwnd_size = qMax(0, _cwnd_size - int(msg_info->_flighting_seqs.size()));
    msg_info->_flighting_seqs.clear();
    msg_info->_rsp_seqs.clear();

    if (success) {
        // 成功才移出传输表；失败保留 MsgInfo，气泡上的“重试”才能找回上传任务
        UserMgr::GetInstance()->RmvTransFile(name);
    }
    if (_active_upload == name) {
        _active_upload.clear();
        StartNextUpload();
    }
}

FileTcpMgr::DownloadCtx* FileTcpMgr::FindDownloadCtx(const QString& name)
{
    for (auto iter = _downloading.begin(); iter != _downloading.end(); ++iter) {
        if (iter->name == name) {
            return &iter.value();
        }
    }
    return nullptr;
}

void FileTcpMgr::PauseDownload(const QString& name)
{
    DownloadCtx* ctx = FindDownloadCtx(name);
    if (!ctx) {
        return;
    }
    ctx->paused = true;
    LLOG_DEBUG << "FileTcpMgr pause download:" << name;
}

void FileTcpMgr::ResumeDownload(const QString& name)
{
    DownloadCtx* ctx = FindDownloadCtx(name);
    if (!ctx) {
        return;
    }
    ctx->paused = false;
    if (!ctx->in_flight) {
        ctx->in_flight = true;
        SendDownloadReq(ctx->name, ctx->owner_uid, ctx->client_path, ctx->next_seq);
    }
}

bool FileTcpMgr::IsDownloadPaused(const QString& name)
{
    DownloadCtx* ctx = FindDownloadCtx(name);
    return ctx != nullptr && ctx->paused;
}

// 发一条 1033 下载请求：字段与老师版对齐（name/seq/token/uid/client_path），
// 多带 owner_uid（老师版靠 Redis 里的 FileInfo 定位路径，本版精简为直接带所有者 uid）
void FileTcpMgr::SendDownloadReq(const QString& fileName, int ownerUid,
                                 const QString& clientPath, int seq)
{
    QJsonObject jsonObj;
    jsonObj["name"] = fileName;
    jsonObj["seq"] = seq;
    jsonObj["token"] = UserMgr::GetInstance()->GetToken();
    jsonObj["uid"] = UserMgr::GetInstance()->GetUid();
    jsonObj["owner_uid"] = ownerUid;
    jsonObj["client_path"] = clientPath;
    QJsonDocument doc(jsonObj);
    sendMsg(RES_ID_DOWNLOAD_FILE_REQ, doc.toJson(QJsonDocument::Compact));
}

// 下载头像入口：本地加载失败时由 LoadAvatarPixmap 触发（见 global.cpp），
// 收到 1057 头像变更通知时也会强制拉一次。
// 去重按 client_path（含所有者 uid）：所有人的头像文件名都是 head.png，
// 按文件名去重会让第二个用户的下载被直接跳过，头像永远不更新
void FileTcpMgr::DownloadAvatar(const QString& iconFileName, int ownerUid)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        LLOG_DEBUG << "FileTcpMgr DownloadAvatar failed: resource server not connected";
        return;
    }
    if (iconFileName.isEmpty()) {
        return;
    }

    // 保存路径与"自己头像"的存储规则一致：AppData/<所有者uid>/avatars/<文件名>，
    // 这样下次加载时 LoadAvatarPixmap 拼同样的路径就能直接命中本地文件
    QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString avatar_dir = storage_dir + "/" + QString::number(ownerUid) + "/avatars";
    QDir dir(avatar_dir);
    if (!dir.exists() && !dir.mkpath(".")) {
        LLOG_DEBUG << "FileTcpMgr DownloadAvatar mkpath failed:" << avatar_dir;
        return;
    }
    QString client_path = dir.filePath(iconFileName);
    if (_downloading.contains(client_path)) {
        return;
    }

    // 头像不做断点续传：所有头像都叫 head.png，旧头像下载失败的半截 .tmp
    // 和"换头像后的新内容"不是同一个文件，续上会拼出坏图。直接清掉从 1 重下
    QFile::remove(client_path + ".tmp");

    DownloadCtx ctx;
    ctx.owner_uid = ownerUid;
    ctx.name = iconFileName;
    ctx.client_path = client_path;
    ctx.next_seq = 1;
    ctx.in_flight = true;
    ctx.resume_allowed = false;
    _downloading.insert(client_path, ctx);

    LLOG_DEBUG << "FileTcpMgr DownloadAvatar start:" << iconFileName
             << "owner" << ownerUid;
    SendDownloadReq(iconFileName, ownerUid, client_path, 1);
}

// 下载聊天图片：图片在服务器按 <发送者uid>/<unique_name> 落盘，
// 本地缓存到 AppData/<我的uid>/chatimg/<发送者uid>/<imgName>。
// 与 DownloadAvatar 走同一条 1033 通道（owner_uid 不同目录就不同），互不影响
void FileTcpMgr::DownloadChatImage(const QString& imgName, int senderUid)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        LLOG_DEBUG << "FileTcpMgr DownloadChatImage failed: resource server not connected";
        return;
    }
    if (imgName.isEmpty()) {
        return;
    }

    QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString img_dir = storage_dir + "/" + QString::number(UserMgr::GetInstance()->GetUid())
                      + "/chatimg/" + QString::number(senderUid);
    QDir dir(img_dir);
    if (!dir.exists() && !dir.mkpath(".")) {
        LLOG_DEBUG << "FileTcpMgr DownloadChatImage mkpath failed:" << img_dir;
        return;
    }
    QString client_path = dir.filePath(imgName);
    if (_downloading.contains(client_path)) {
        return;   //已在下载中（多气泡同一张图只发一次请求）
    }

    // 断点续传：上次下到一半的 .tmp 还在，就按它的大小算出下一块 seq，
    // 不再从第 1 块重下。块大小 32KB，半块 .tmp（非整块）说明数据不可用，删掉重下
    int start_seq = 1;
    QFileInfo tmp_info(client_path + ".tmp");
    if (tmp_info.exists() && tmp_info.size() > 0) {
        if (tmp_info.size() % MAX_FILE_LEN == 0) {
            start_seq = int(tmp_info.size() / MAX_FILE_LEN) + 1;
            LLOG_DEBUG << "FileTcpMgr DownloadChatImage resume from seq" << start_seq
                     << imgName;
        }
        else {
            QFile::remove(client_path + ".tmp");
        }
    }

    DownloadCtx ctx;
    ctx.owner_uid = senderUid;   //服务器按它拼 static/<uid>/<name> 定位图片
    ctx.name = imgName;
    ctx.client_path = client_path;
    ctx.next_seq = start_seq;
    ctx.in_flight = true;
    ctx.resume_allowed = true;   //唯一文件名，安全续传
    _downloading.insert(client_path, ctx);

    LLOG_DEBUG << "FileTcpMgr DownloadChatImage start:" << imgName
             << "sender" << senderUid << "seq" << start_seq;
    SendDownloadReq(imgName, senderUid, ctx.client_path, start_seq);
}

// 下载聊天文件（day41）：与 DownloadChatImage 完全同构，只是缓存目录换成
// AppData/<我的uid>/chatfile/<发送者uid>/（文件卡片下载的是文件本体，
// 与气泡预览用的图片缓存分开，互不干扰）
// 下载聊天文件（day41）：临时文件仍按 unique_name 放 AppData 保证断点续传，
// 下载完成后另存到“下载/llfcchat/原始文件名”，并写入本地路径索引
void FileTcpMgr::DownloadChatFile(const QString& fileName, int senderUid,
                                  const QString& originalName)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        LLOG_DEBUG << "FileTcpMgr DownloadChatFile failed: resource server not connected";
        return;
    }
    if (fileName.isEmpty()) {
        return;
    }

    // 临时目录：保持 UUID 命名，暂停/重启后按 .tmp 续传
    QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString file_dir = storage_dir + "/" + QString::number(UserMgr::GetInstance()->GetUid())
                       + "/chatfile/" + QString::number(senderUid);
    QDir dir(file_dir);
    if (!dir.exists() && !dir.mkpath(".")) {
        LLOG_DEBUG << "FileTcpMgr DownloadChatFile mkpath failed:" << file_dir;
        return;
    }
    QString client_path = dir.filePath(fileName);
    if (_downloading.contains(client_path)) {
        return;
    }

    // 最终保存路径：下载目录/llfcchat/原始文件名（重名自动加序号）
    QString save_dir = ResolveDownloadDir();
    if (save_dir.isEmpty()) {
        LLOG_DEBUG << "FileTcpMgr DownloadChatFile: cannot resolve download dir";
        return;
    }
    QString save_name = QFileInfo(originalName).fileName();   //去掉可能的路径部分
    if (save_name.isEmpty()) {
        save_name = fileName;                                  //老服务端没有原始名时兜底
    }
    QString final_path = MakeUniqueFinalPath(save_dir, save_name);

    // 与聊天图片同构：.tmp 有半截就从对应 seq 续下
    int start_seq = 1;
    QFileInfo tmp_info(client_path + ".tmp");
    if (tmp_info.exists() && tmp_info.size() > 0) {
        if (tmp_info.size() % MAX_FILE_LEN == 0) {
            start_seq = int(tmp_info.size() / MAX_FILE_LEN) + 1;
            LLOG_DEBUG << "FileTcpMgr DownloadChatFile resume from seq" << start_seq
                     << fileName;
        }
        else {
            QFile::remove(client_path + ".tmp");
        }
    }

    DownloadCtx ctx;
    ctx.owner_uid = senderUid;
    ctx.name = fileName;
    ctx.client_path = client_path;
    ctx.final_path = final_path;
    ctx.next_seq = start_seq;
    ctx.in_flight = true;
    ctx.resume_allowed = true;
    _downloading.insert(client_path, ctx);

    LLOG_DEBUG << "FileTcpMgr DownloadChatFile start:" << fileName
             << "sender" << senderUid << "seq" << start_seq
             << "save as" << final_path;
    SendDownloadReq(fileName, senderUid, ctx.client_path, start_seq);
}

// 路径索引文件：AppData/<uid>/file_index.ini
QString FileTcpMgr::FileIndexPath() const
{
    int uid = UserMgr::GetInstance()->GetUid();
    if (uid <= 0) {
        return QString();
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/" + QString::number(uid) + "/file_index.ini";
}

QString FileTcpMgr::LookupSavedPath(const QString& uniqueName) const
{
    if (uniqueName.isEmpty()) {
        return QString();
    }
    QString index_path = FileIndexPath();
    if (index_path.isEmpty()) {
        return QString();
    }
    QSettings settings(index_path, QSettings::IniFormat);
    return settings.value("files/" + uniqueName).toString();
}

void FileTcpMgr::RememberSavedPath(const QString& uniqueName, const QString& path)
{
    if (uniqueName.isEmpty() || path.isEmpty()) {
        return;
    }
    QString index_path = FileIndexPath();
    if (index_path.isEmpty()) {
        return;
    }
    QDir().mkpath(QFileInfo(index_path).absolutePath());
    QSettings settings(index_path, QSettings::IniFormat);
    settings.setValue("files/" + uniqueName, path);
    settings.sync();
}

// 用户可见的下载目录：<系统下载目录>/llfcchat
QString FileTcpMgr::ResolveDownloadDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Downloads";
    }
    QString dir_path = base + "/llfcchat";
    QDir dir(dir_path);
    if (!dir.exists() && !dir.mkpath(".")) {
        return QString();
    }
    return dir_path;
}

// 同名文件自动加 (1)(2)...，不覆盖用户已有文件
QString FileTcpMgr::MakeUniqueFinalPath(const QString& dir, const QString& fileName) const
{
    QFileInfo info(fileName);
    QString base = info.completeBaseName();
    QString suffix = info.suffix();
    QString candidate = dir + "/" + fileName;
    for (int i = 1; QFileInfo::exists(candidate) && i < 1000; ++i) {
        candidate = dir + "/" + base + " (" + QString::number(i) + ")"
                    + (suffix.isEmpty() ? QString() : "." + suffix);
    }
    return candidate;
}

// 上传聊天图片/文件（1037）：发送方收到 1036（拿到 message_id）后调用。
// day40：不再"一口气分块发完"，改成拥塞窗口调度：
//   1. 同一时刻整条连接最多 MAX_CWND_SIZE 块在途（_cwnd_size 计数）；
//   2. 每块到达服务器后回 1038，收到回执才把下一块放进窗口；
//   3. 多个文件同时发时排队，避免多个文件互相抢窗口导致谁都传不完。
// 为什么这样做：一口气把大文件塞满发送缓冲，任何一块异常都只能整文件重发；
// 滑窗把在途量限制在可控范围，也是后面做"断点续传"的基础
bool FileTcpMgr::UploadChatImage(const QString& uniqueName)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        LLOG_DEBUG << "FileTcpMgr UploadChatImage failed: resource server not connected";
        return false;
    }

    auto msg_info = UserMgr::GetInstance()->GetTransFileByName(uniqueName);
    if (!msg_info) {
        LLOG_DEBUG << "FileTcpMgr UploadChatImage: trans file not found:" << uniqueName;
        return false;
    }
    if (msg_info->_msg_id <= 0) {
        LLOG_DEBUG << "FileTcpMgr UploadChatImage: message_id not ready for" << uniqueName;
        return false;
    }
    if (msg_info->_total_size <= 0) {
        LLOG_DEBUG << "FileTcpMgr UploadChatImage: empty file, skip" << uniqueName;
        return false;
    }

    // 初始化窗口状态。重发/重入时也重置，保证从第 1 块重新开始
    msg_info->_max_seq = (msg_info->_total_size + MAX_FILE_LEN - 1) / MAX_FILE_LEN;
    msg_info->_seq = 0;
    msg_info->_rsp_size = 0;
    msg_info->_last_confirmed_seq = 0;
    msg_info->_flighting_seqs.clear();
    msg_info->_rsp_seqs.clear();
    // 新上传/重发：状态复位为上传中，暂停按钮/进度条据此显示
    msg_info->_transfer_state = TransferState::Uploading;
    msg_info->_transfer_type = TransferType::Upload;

    // 窗口正被别的文件占用：先排队，等它传完由 StartNextUpload 拉起
    if (!_active_upload.isEmpty() && _active_upload != uniqueName) {
        if (!_pending_uploads.contains(uniqueName)) {
            _pending_uploads.enqueue(uniqueName);
        }
        LLOG_DEBUG << "FileTcpMgr upload queued:" << uniqueName
                 << "pending" << _pending_uploads.size();
        return true;
    }

    _active_upload = uniqueName;
    BatchSend(msg_info);
    return true;
}

// 按拥塞窗口发送：从"已发出的最后一块"之后继续读文件，
// 直到窗口塞满（_cwnd_size == MAX_CWND_SIZE）或文件发完。
// 每发一块：_seq++、记入 _flighting_seqs（等回执）、_cwnd_size++
void FileTcpMgr::BatchSend(std::shared_ptr<MsgInfo> msg_info)
{
    if (!msg_info) {
        return;
    }
    // 暂停状态不补发新块：已发出的块照常等回执，恢复时再走 1005 同步进度
    if (msg_info->_transfer_state != TransferState::Uploading) {
        return;
    }
    if (_cwnd_size >= MAX_CWND_SIZE) {
        return;   // 窗口满：等在途分块的 1038 回执来放行
    }
    if (msg_info->_seq >= msg_info->_max_seq) {
        return;   // 已经全部发出，只等最后几块回执
    }

    QFile file(msg_info->content);   //content 存的是本地路径
    if (!file.open(QIODevice::ReadOnly)) {
        LLOG_DEBUG << "FileTcpMgr BatchSend could not open file:"
                 << msg_info->content << file.errorString();
        return;
    }
    // 文件指针拨到"已发位置"之后：已发出的块不再重读，
    // 回执驱动时只从这里往后补新块
    file.seek(msg_info->_seq * MAX_FILE_LEN);

    while (_cwnd_size < MAX_CWND_SIZE && msg_info->_seq < msg_info->_max_seq) {
        QByteArray buffer = file.read(MAX_FILE_LEN);
        if (buffer.isEmpty()) {
            break;
        }

        ++msg_info->_seq;
        msg_info->_flighting_seqs.insert(static_cast<int>(msg_info->_seq));
        qint64 trans_size = (msg_info->_seq - 1) * MAX_FILE_LEN + buffer.size();

        QJsonObject jsonObj;
        jsonObj["md5"] = msg_info->_md5;
        jsonObj["name"] = msg_info->_unique_name;
        jsonObj["seq"] = static_cast<int>(msg_info->_seq);
        jsonObj["trans_size"] = static_cast<int>(trans_size);
        jsonObj["total_size"] = static_cast<int>(msg_info->_total_size);
        // 最后一块用序号判断，不靠"读不满 32KB"——文件大小正好是块整数倍时
        // 最后一块也是满的，按长度判断会漏发 last=1，服务端永远不收尾
        jsonObj["last"] = (msg_info->_seq == msg_info->_max_seq) ? 1 : 0;
        jsonObj["data"] = QString::fromLatin1(buffer.toBase64());
        //身份字段：首块校验 token；message_id/sender/receiver 供服务端
        //落盘完成后更新消息状态并通知对方
        jsonObj["token"] = UserMgr::GetInstance()->GetToken();
        jsonObj["uid"] = UserMgr::GetInstance()->GetUid();
        jsonObj["message_id"] = msg_info->_msg_id;
        jsonObj["sender"] = msg_info->_sender;
        jsonObj["receiver"] = msg_info->_receiver;

        QJsonDocument doc(jsonObj);
        sendMsg(RES_ID_IMG_CHAT_UPLOAD_REQ, doc.toJson(QJsonDocument::Compact));
        ++_cwnd_size;
    }

    file.close();
    LLOG_DEBUG << "FileTcpMgr BatchSend:" << msg_info->_unique_name
             << "sent seq" << msg_info->_seq << "/" << msg_info->_max_seq
             << "cwnd" << _cwnd_size;
}

// 当前文件全部确认后调用：从排队队列里取下一个可传的文件。
// 队列里的名字可能在排队期间被删掉（切会话/取消），取不到就跳过
void FileTcpMgr::StartNextUpload()
{
    while (!_pending_uploads.isEmpty()) {
        QString name = _pending_uploads.dequeue();
        auto msg_info = UserMgr::GetInstance()->GetTransFileByName(name);
        if (!msg_info || msg_info->_msg_id <= 0) {
            continue;
        }
        _active_upload = name;
        LLOG_DEBUG << "FileTcpMgr start next upload:" << name;
        BatchSend(msg_info);
        return;
    }
}
