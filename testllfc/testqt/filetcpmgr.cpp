#include "filetcpmgr.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
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
        qDebug() << "FileTcpMgr connected to resource server!";
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
        qDebug() << "FileTcpMgr socket error:" << _socket.errorString();
        // 只把"根本没连上"的致命错误当连接失败上报（sig_res_connect(false)），
        // 断线场景由下面的 disconnected 信号统一处理，避免重复通知
        if (socketError == QAbstractSocket::ConnectionRefusedError ||
            socketError == QAbstractSocket::HostNotFoundError) {
            emit sig_res_connect(false);
        }
    });

    // 断开：资源服务器主动关连接或网络异常都会走到这里
    QObject::connect(&_socket, &QTcpSocket::disconnected, [this]() {
        qDebug() << "FileTcpMgr disconnected from resource server.";
        // 断线后必须清掉残留的半包状态，否则下次重连会把旧数据当新包头解析
        _buffer.clear();
        _b_recv_pending = false;
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
        qDebug() << "FileTcpMgr handle id is " << id;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            qDebug() << "FileTcpMgr test rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            qDebug() << "FileTcpMgr test rsp missing error field";
            return;
        }
        if (jsonObj["error"].toInt() != ErrorCodes::SUCCESS) {
            qDebug() << "FileTcpMgr test rsp error is " << jsonObj["error"].toInt();
            return;
        }
        // 回显内容通过信号抛出去；当前阶段没有界面接收，收到只打日志
        emit sig_test_rsp(jsonObj["data"].toString());
    });

    // 1004 上传进度回执：服务器每落盘一块就回一条，格式
    // {error, total_size, seq, name, trans_size, last}
    _handlers.insert(RES_ID_UPLOAD_FILE_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "FileTcpMgr handle id is " << id;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            qDebug() << "FileTcpMgr upload rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            qDebug() << "FileTcpMgr upload rsp missing error field";
            return;
        }
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "FileTcpMgr upload rsp error is " << err;
            return;
        }

        QString name = jsonObj["name"].toString();
        int trans_size = jsonObj["trans_size"].toInt();  // 服务器已收到的字节数
        int total_size = jsonObj["total_size"].toInt();  // 文件总大小
        int last = jsonObj["last"].toInt();              // 1 表示服务器已收完最后一块

        // 进度信号：界面拿它刷进度条（trans_size/total_size 算百分比）
        emit sig_upload_progress(trans_size, total_size);
        if (last == 1) {
            qDebug() << "FileTcpMgr upload done:" << name;
            emit sig_upload_done(name);
        }
    });

    // 1032 头像上传回包：服务器每落盘一块就回一条
    // {error, total_size, seq, name, trans_size, last, md5, uid, last_seq}。
    // 与 1004 的差别：1031 是"落盘后才回"，且最后一块的回包发在 DB/Redis 更新之后，
    // 所以收到 trans_size==total_size 的 1032 就代表"头像已完整入库"，而不是仅仅收完
    _handlers.insert(RES_ID_UPLOAD_HEAD_ICON_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "FileTcpMgr handle id is " << id;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            qDebug() << "FileTcpMgr head icon rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            qDebug() << "FileTcpMgr head icon rsp missing error field";
            return;
        }
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            // 常见错误：1010 token 失效 / 1011 uid 无效（Redis 查不到 token）——
            // 一般是登录态过期后仍尝试上传，提示用户重新登录即可
            qDebug() << "FileTcpMgr head icon upload failed, err is" << err;
            return;
        }

        QString name = jsonObj["name"].toString();
        int trans_size = jsonObj["trans_size"].toInt();
        int total_size = jsonObj["total_size"].toInt();
        emit sig_upload_progress(trans_size, total_size);
        if (trans_size == total_size) {
            qDebug() << "FileTcpMgr head icon upload done:" << name
                     << "total_size" << total_size;
            emit sig_upload_head_done(name);
        }
    });

    // 1038 图片上传进度回包：{error, total_size, seq, name, trans_size, last,
    // message_id, sender, receiver}，每块落盘后服务器回一条（与 1004 同构）。
    // 最后一块回包到达只代表"文件已落盘"，DB 状态更新和对方通知在服务端
    // FileWorker 线程里紧随其后异步完成
    _handlers.insert(RES_ID_IMG_CHAT_UPLOAD_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            qDebug() << "FileTcpMgr img chat upload rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            qDebug() << "FileTcpMgr img chat upload rsp missing error field";
            return;
        }
        int err = jsonObj["error"].toInt();
        QString name = jsonObj["name"].toString();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "FileTcpMgr img chat upload failed, err is" << err;
            return;
        }

        int trans_size = jsonObj["trans_size"].toInt();
        int total_size = jsonObj["total_size"].toInt();
        emit sig_upload_progress(trans_size, total_size);
        if (jsonObj["last"].toInt() == 1) {
            qDebug() << "FileTcpMgr img chat upload done:" << name
                     << "message_id" << jsonObj["message_id"].toInt();
            //传输结束：从管理表移除（接收方的同名占位登记是独立的，不受影响）
            UserMgr::GetInstance()->RmvTransFile(name);
        }
    });

    // 1034 下载回包：服务器按 (seq-1)*32KB 偏移读一块 base64 回来。
    // {error, data, seq, total_size, current_size, is_last, name, client_path}。
    // 客户端按 seq 覆盖/追加写本地文件；is_last=0 就带 seq+1 继续请求，
    // is_last=1 清掉下载上下文并发 sig_avatar_downloaded 通知界面刷新
    _handlers.insert(RES_ID_DOWNLOAD_FILE_RSP, [this](quint16 id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            qDebug() << "FileTcpMgr download rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            qDebug() << "FileTcpMgr download rsp missing error field";
            return;
        }
        int err = jsonObj["error"].toInt();
        QString name = jsonObj["name"].toString();
        if (err != ErrorCodes::SUCCESS) {
            // 常见错误：1012 文件不存在（对方从未上传过头像，icon 还是默认编号
            // 时不会走到下载，所以一般是异常数据）、1010/1011 登录态失效
            qDebug() << "FileTcpMgr download failed, err is" << err << "name is" << name;
            _downloading.remove(name);
            //通知界面恢复可重试状态（文件卡片状态会卡在"下载中"，图片气泡占位不变）
            emit sig_download_failed(name);
            return;
        }

        auto ctx_iter = _downloading.find(name);
        if (ctx_iter == _downloading.end()) {
            qDebug() << "FileTcpMgr download rsp for unknown file:" << name;
            return;
        }

        int seq = jsonObj["seq"].toInt();
        bool is_last = jsonObj["is_last"].toBool();
        QByteArray decoded = QByteArray::fromBase64(
            jsonObj["data"].toString().toUtf8());

        // seq==1 覆盖写（新文件），后续块追加——与上传的落盘规则对称
        QFile file(ctx_iter->client_path);
        QIODevice::OpenMode mode = (seq == 1)
            ? QIODevice::WriteOnly
            : (QIODevice::WriteOnly | QIODevice::Append);
        if (!file.open(mode)) {
            qDebug() << "FileTcpMgr download open file failed:" << ctx_iter->client_path
                     << file.errorString();
            _downloading.remove(name);
            return;
        }
        file.write(decoded);
        file.close();

        if (is_last) {
            qDebug() << "FileTcpMgr download done:" << name
                     << "->" << ctx_iter->client_path;
            _downloading.remove(name);
            // 本地文件已就绪：通知界面把还显示着占位图的头像/图片气泡重新加载
            emit sig_file_downloaded(name);
        }
        else {
            // 非最后一块：seq+1 继续请求下一块
            int next_seq = ctx_iter->next_seq + 1;
            ctx_iter->next_seq = next_seq;
            SendDownloadReq(name, ctx_iter->owner_uid,
                            ctx_iter->client_path, next_seq);
        }
    });
}

void FileTcpMgr::handleMsg(quint16 id, int len, QByteArray data)
{
    auto find_iter = _handlers.find(id);
    if (find_iter == _handlers.end()) {
        // 未知 ID 不处理：资源服务器后续加新消息类型时客户端没跟上也不会崩
        qDebug() << "FileTcpMgr not found id [" << id << "] to handle";
        return;
    }
    find_iter.value()(id, len, data);
}

// 连接资源服务器：host 形如 "127.0.0.1"，port 一般 9090
// 结果通过 sig_res_connect(bool) 异步反馈（连接成功/失败都是事件驱动的）
void FileTcpMgr::ConnectResServer(const QString& host, quint16 port)
{
    qDebug() << "FileTcpMgr connecting to resource server" << host << ":" << port;
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
        qDebug() << "FileTcpMgr slot_send_data: not connected, drop msg id" << id;
        return;
    }

    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);  // 和服务器约定的大端一致
    out << id << static_cast<quint32>(body.size());  // 2B id + 4B 长度 = 6B 包头
    block.append(body);                        // 接上 JSON 包体

    _socket.write(block);
}

// 上传文件：参考 day31 文档的 on_uploadBtn_clicked 实现（简化版，不做断点续传）
// 流程：只读打开 → 整文件算 md5 → 指针拨回 → 按 32KB 循环分块 → JSON 组包 → 逐块发送
bool FileTcpMgr::UploadFile(QString path)
{
    // 发送前先确认资源服务器链路可用，否则分块发出去没人收，
    // 只会把数据堆进发送缓冲区白白占用内存
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        qDebug() << "FileTcpMgr UploadFile failed: resource server not connected";
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "FileTcpMgr could not open file:" << file.errorString();
        return false;
    }

    // 计算整个文件的 MD5：md5 是文件内容的"指纹"，
    // 服务器拿它标识文件，将来断点续传/秒传都要靠它判断"传的是不是同一个文件"
    QCryptographicHash hash(QCryptographicHash::Md5);
    qint64 originalPos = file.pos();   // 保存当前文件指针位置
    if (!hash.addData(&file)) {        // addData(&QFile) 会一直读到文件尾
        qDebug() << "FileTcpMgr failed to read data from file:" << path;
        file.close();
        return false;
    }
    QString file_md5 = hash.result().toHex();  // 32 位十六进制字符串

    QFileInfo fileInfo(path);
    QString fileName = fileInfo.fileName();    // 只发文件名，不带本地路径
    qint64 total_size = fileInfo.size();
    if (total_size <= 0) {
        // 空文件没有可传的分块，直接拒绝，避免服务器收到 last_seq=0 收不了尾
        qDebug() << "FileTcpMgr UploadFile: empty file, skip";
        file.close();
        return false;
    }

    // 总块数（向上取整）：last_seq 是最后一块的序号，收尾判断以它为准
    qint64 last_seq = (total_size + MAX_FILE_LEN - 1) / MAX_FILE_LEN;

    // 算 md5 时文件指针已被读到尾部，拨回起点再按块重读内容
    file.seek(originalPos);

    qint64 seq = 0;
    while (!file.atEnd()) {
        QByteArray buffer = file.read(MAX_FILE_LEN);  // 每次读一块（32KB）
        ++seq;  // seq 从 1 开始，服务器按 seq 顺序追加写文件

        QJsonObject jsonObj;
        jsonObj["md5"] = file_md5;
        jsonObj["name"] = fileName;
        jsonObj["seq"] = static_cast<int>(seq);
        // trans_size：截至本块累计已传字节数 = 本块大小 + 前面整块的固定大小
        jsonObj["trans_size"] = static_cast<int>(buffer.size() + (seq - 1) * MAX_FILE_LEN);
        jsonObj["total_size"] = static_cast<int>(total_size);
        // 用 seq == last_seq 判断最后一块，而不是 day31 的"读不满 32KB 就是最后一块"：
        // 文件大小恰好是 32KB 整数倍时最后一块也读得满满的，
        // 按"读不满"判断会漏发 last=1，服务器永远等不到收尾信号
        jsonObj["last"] = (seq == last_seq) ? 1 : 0;
        jsonObj["last_seq"] = static_cast<int>(last_seq);
        // 分块数据 base64 编码后塞进 JSON：JSON 文本协议没法直接携带二进制，
        // base64 后体积涨约 1/3，但 32KB 一块（约 44KB 编码后）服务器完全接受
        jsonObj["data"] = QString::fromLatin1(buffer.toBase64());

        QJsonDocument doc(jsonObj);
        // 走 sendMsg → sig_send_data → slot_send_data 的信号链发送
        sendMsg(RES_ID_UPLOAD_FILE_REQ, doc.toJson(QJsonDocument::Compact));
    }

    file.close();
    qDebug() << "FileTcpMgr UploadFile sent:" << fileName
             << "total_size" << total_size << "blocks" << seq;
    return true;
}

// 上传头像（老师消息 1031 链路）：分块组包逻辑与 UploadFile 完全同构，差别有三点——
// 1. 消息 ID 用 1031；请求 JSON 额外带 token/uid（服务端首块时校验登录态）
// 2. 服务端按 uid 分目录落盘并更新 MySQL/Redis，所以文件名固定为 head.png 也能
//    按 uid 区分；md5 只是随老师协议带上，本版不用它做断点续传
// 3. 完成信号用 sig_upload_head_done（服务端 DB 更新完才回的 1032）
bool FileTcpMgr::UploadHeadIcon(QString path)
{
    // 和 UploadFile 一样：链路没通就直接失败，不做无谓的组包
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        qDebug() << "FileTcpMgr UploadHeadIcon failed: resource server not connected";
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "FileTcpMgr UploadHeadIcon could not open file:" << file.errorString();
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Md5);
    qint64 originalPos = file.pos();
    if (!hash.addData(&file)) {
        qDebug() << "FileTcpMgr UploadHeadIcon failed to read data from file:" << path;
        file.close();
        return false;
    }
    QString file_md5 = hash.result().toHex();

    QFileInfo fileInfo(path);
    // 服务器端按 <uid>/<文件名> 落盘，这里只发文件名不带本地路径
    QString fileName = fileInfo.fileName();
    qint64 total_size = fileInfo.size();
    if (total_size <= 0) {
        qDebug() << "FileTcpMgr UploadHeadIcon: empty file, skip";
        file.close();
        return false;
    }

    qint64 last_seq = (total_size + MAX_FILE_LEN - 1) / MAX_FILE_LEN;

    file.seek(originalPos);

    qint64 seq = 0;
    while (!file.atEnd()) {
        QByteArray buffer = file.read(MAX_FILE_LEN);
        ++seq;

        QJsonObject jsonObj;
        jsonObj["md5"] = file_md5;
        jsonObj["name"] = fileName;
        jsonObj["seq"] = static_cast<int>(seq);
        jsonObj["trans_size"] = static_cast<int>(buffer.size() + (seq - 1) * MAX_FILE_LEN);
        jsonObj["total_size"] = static_cast<int>(total_size);
        jsonObj["last"] = (seq == last_seq) ? 1 : 0;
        jsonObj["last_seq"] = static_cast<int>(last_seq);
        // 头像请求比 1003 多带的两个身份字段：
        // 服务端首块时拿 token 和 Redis 里 utoken_<uid> 比对，通过才落盘
        jsonObj["token"] = UserMgr::GetInstance()->GetToken();
        jsonObj["uid"] = UserMgr::GetInstance()->GetUid();
        jsonObj["data"] = QString::fromLatin1(buffer.toBase64());

        QJsonDocument doc(jsonObj);
        sendMsg(RES_ID_UPLOAD_HEAD_ICON_REQ, doc.toJson(QJsonDocument::Compact));
    }

    file.close();
    qDebug() << "FileTcpMgr UploadHeadIcon sent:" << fileName
             << "total_size" << total_size << "blocks" << seq;
    return true;
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

// 下载头像入口：本地加载失败时由 LoadAvatarPixmap 触发（见 global.cpp）。
// 去重：同一文件名已在下载中就不再发请求（一个会话列表里同一个头像
// 可能被多个条目触发多次，重复下载会互相覆盖写文件）
void FileTcpMgr::DownloadAvatar(const QString& iconFileName, int ownerUid)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        qDebug() << "FileTcpMgr DownloadAvatar failed: resource server not connected";
        return;
    }
    if (iconFileName.isEmpty() || _downloading.contains(iconFileName)) {
        return;
    }

    // 保存路径与"自己头像"的存储规则一致：AppData/<所有者uid>/avatars/<文件名>，
    // 这样下次加载时 LoadAvatarPixmap 拼同样的路径就能直接命中本地文件
    QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString avatar_dir = storage_dir + "/" + QString::number(ownerUid) + "/avatars";
    QDir dir(avatar_dir);
    if (!dir.exists() && !dir.mkpath(".")) {
        qDebug() << "FileTcpMgr DownloadAvatar mkpath failed:" << avatar_dir;
        return;
    }
    QString client_path = dir.filePath(iconFileName);

    DownloadCtx ctx;
    ctx.owner_uid = ownerUid;
    ctx.client_path = client_path;
    ctx.next_seq = 1;
    _downloading.insert(iconFileName, ctx);

    qDebug() << "FileTcpMgr DownloadAvatar start:" << iconFileName
             << "owner" << ownerUid;
    SendDownloadReq(iconFileName, ownerUid, client_path, 1);
}

// 下载聊天图片：图片在服务器按 <发送者uid>/<unique_name> 落盘，
// 本地缓存到 AppData/<我的uid>/chatimg/<发送者uid>/<imgName>。
// 与 DownloadAvatar 走同一条 1033 通道（owner_uid 不同目录就不同），互不影响
void FileTcpMgr::DownloadChatImage(const QString& imgName, int senderUid)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        qDebug() << "FileTcpMgr DownloadChatImage failed: resource server not connected";
        return;
    }
    if (imgName.isEmpty() || _downloading.contains(imgName)) {
        return;   //空名 / 已在下载中（多气泡同名图片只发一次请求）
    }

    QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString img_dir = storage_dir + "/" + QString::number(UserMgr::GetInstance()->GetUid())
                      + "/chatimg/" + QString::number(senderUid);
    QDir dir(img_dir);
    if (!dir.exists() && !dir.mkpath(".")) {
        qDebug() << "FileTcpMgr DownloadChatImage mkpath failed:" << img_dir;
        return;
    }

    DownloadCtx ctx;
    ctx.owner_uid = senderUid;   //服务器按它拼 static/<uid>/<name> 定位图片
    ctx.client_path = dir.filePath(imgName);
    ctx.next_seq = 1;
    _downloading.insert(imgName, ctx);

    qDebug() << "FileTcpMgr DownloadChatImage start:" << imgName
             << "sender" << senderUid;
    SendDownloadReq(imgName, senderUid, ctx.client_path, 1);
}

// 下载聊天文件（day41）：与 DownloadChatImage 完全同构，只是缓存目录换成
// AppData/<我的uid>/chatfile/<发送者uid>/（文件卡片下载的是文件本体，
// 与气泡预览用的图片缓存分开，互不干扰）
void FileTcpMgr::DownloadChatFile(const QString& fileName, int senderUid)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        qDebug() << "FileTcpMgr DownloadChatFile failed: resource server not connected";
        return;
    }
    if (fileName.isEmpty() || _downloading.contains(fileName)) {
        return;
    }

    QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString file_dir = storage_dir + "/" + QString::number(UserMgr::GetInstance()->GetUid())
                       + "/chatfile/" + QString::number(senderUid);
    QDir dir(file_dir);
    if (!dir.exists() && !dir.mkpath(".")) {
        qDebug() << "FileTcpMgr DownloadChatFile mkpath failed:" << file_dir;
        return;
    }

    DownloadCtx ctx;
    ctx.owner_uid = senderUid;
    ctx.client_path = dir.filePath(fileName);
    ctx.next_seq = 1;
    _downloading.insert(fileName, ctx);

    qDebug() << "FileTcpMgr DownloadChatFile start:" << fileName
             << "sender" << senderUid;
    SendDownloadReq(fileName, senderUid, ctx.client_path, 1);
}

// 上传聊天图片（1037）：发送方收到 1036（拿到 message_id）后调用。
// 与 UploadHeadIcon 同构的"一口气分块"，但每块多带 message_id/sender/receiver：
// 服务端最后一块落盘后要更新 chat_message 状态并 gRPC 通知对方
bool FileTcpMgr::UploadChatImage(const QString& uniqueName)
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        qDebug() << "FileTcpMgr UploadChatImage failed: resource server not connected";
        return false;
    }

    auto msg_info = UserMgr::GetInstance()->GetTransFileByName(uniqueName);
    if (!msg_info) {
        qDebug() << "FileTcpMgr UploadChatImage: trans file not found:" << uniqueName;
        return false;
    }
    if (msg_info->_msg_id <= 0) {
        qDebug() << "FileTcpMgr UploadChatImage: message_id not ready for" << uniqueName;
        return false;
    }

    QFile file(msg_info->content);   //content 存的是本地路径
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "FileTcpMgr UploadChatImage could not open file:"
                 << msg_info->content << file.errorString();
        return false;
    }

    qint64 total_size = msg_info->_total_size;
    qint64 last_seq = (total_size + MAX_FILE_LEN - 1) / MAX_FILE_LEN;

    qint64 seq = 0;
    while (!file.atEnd()) {
        QByteArray buffer = file.read(MAX_FILE_LEN);
        ++seq;

        QJsonObject jsonObj;
        jsonObj["md5"] = msg_info->_md5;
        jsonObj["name"] = uniqueName;
        jsonObj["seq"] = static_cast<int>(seq);
        jsonObj["trans_size"] = static_cast<int>(buffer.size() + (seq - 1) * MAX_FILE_LEN);
        jsonObj["total_size"] = static_cast<int>(total_size);
        jsonObj["last"] = (seq == last_seq) ? 1 : 0;
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
    }

    file.close();
    qDebug() << "FileTcpMgr UploadChatImage sent:" << uniqueName
             << "total_size" << total_size << "blocks" << seq;
    return true;
}
