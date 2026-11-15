#include "filetcpmgr.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>

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
