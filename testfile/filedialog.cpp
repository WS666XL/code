#include "filedialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDataStream>
#include <QDebug>

FileDialog::FileDialog(QWidget* parent)
    : QDialog(parent), _b_recv_pending(false), _message_id(0), _message_len(0)
{
    setWindowTitle(tr("文件传输联调 Demo（ResourceServer 127.0.0.1:9090）"));
    resize(460, 180);

    // ---- 纯代码搭 UI：两行布局（按钮行 + 进度条 + 状态文字）----
    _select_btn = new QPushButton(tr("选择文件"), this);
    _upload_btn = new QPushButton(tr("上传"), this);
    _upload_btn->setEnabled(false);   // 没选文件前不允许点上传
    _progress_bar = new QProgressBar(this);
    _progress_bar->setRange(0, 100);
    _progress_bar->setValue(0);
    _status_label = new QLabel(tr("正在连接资源服务器 127.0.0.1:9090 ..."), this);

    auto* btn_row = new QHBoxLayout();
    btn_row->addWidget(_select_btn);
    btn_row->addWidget(_upload_btn);
    btn_row->addStretch();            // 按钮靠左排，右侧留白
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(btn_row);
    layout->addWidget(_progress_bar);
    layout->addWidget(_status_label);

    // ---- 按钮信号 ----
    connect(_select_btn, &QPushButton::clicked, this, &FileDialog::slot_select_file);
    connect(_upload_btn, &QPushButton::clicked, this, &FileDialog::slot_upload_file);

    // ---- 网络信号 ----
    // 连接成功：发一条 1001 链路自测，整条链路（组包/解包/JSON）先通一遍，
    // 也顺便向使用者确认服务器活着
    connect(&_socket, &QTcpSocket::connected, [this]() {
        qDebug() << "FileDialog connected to resource server!";
        _status_label->setText(tr("已连接资源服务器，请选择文件"));

        QJsonObject jsonObj;
        jsonObj["data"] = "hello";
        sendMsg(RES_ID_TEST_MSG_REQ, QJsonDocument(jsonObj).toJson(QJsonDocument::Compact));
    });

    // 断开连接：清残留半包状态，防止下次重连把旧数据当新包头解析
    connect(&_socket, &QTcpSocket::disconnected, [this]() {
        qDebug() << "FileDialog disconnected from resource server.";
        _buffer.clear();
        _b_recv_pending = false;
        _status_label->setText(tr("与资源服务器的连接已断开"));
    });

    // 错误信号（Qt 5.15+）：把错误串显示到状态栏，联调时最直观
    connect(&_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
        [this](QAbstractSocket::SocketError socketError) {
        Q_UNUSED(socketError);
        qDebug() << "FileDialog socket error:" << _socket.errorString();
        _status_label->setText(tr("网络错误：%1").arg(_socket.errorString()));
    });

    // 收到数据：读到的只是字节流，全部先进缓冲区，统一交给 processData 切包
    connect(&_socket, &QTcpSocket::readyRead, this, &FileDialog::slot_ready_read);

    // 发送走"信号转槽"（day31 文档原样写法）：外部只 emit sig_send_msg，
    // 真正的 write 在槽里执行，多线程下也安全
    connect(this, &FileDialog::sig_send_msg, this, &FileDialog::slot_send_msg);

    // 网络异常提示：统一更新到状态文字并恢复上传按钮，方便重新操作
    connect(this, &FileDialog::sig_net_error, this, [this](QString err) {
        _status_label->setText(err);
        _upload_btn->setEnabled(true);
    });

    // 解出完整包后按 id 分发：进度条更新、完成提示都在这里
    connect(this, &FileDialog::sig_logic_process, this, [this](quint16 msg_id, QJsonObject json) {
        switch (msg_id) {
        case RES_ID_TEST_MSG_RSP:
            // 1002 链路自测回包：服务器把 data 原样带回，收到即说明链路是通的
            qDebug() << "FileDialog test rsp data is" << json["data"].toString();
            _status_label->setText(tr("链路自测成功（data=%1）").arg(json["data"].toString()));
            break;
        case RES_ID_UPLOAD_FILE_RSP: {
            // 1004 上传进度回执：{error, total_size, seq, name, trans_size, last}
            if (json["error"].toInt() != 0) {
                _status_label->setText(tr("上传出错，服务器返回 error=%1").arg(json["error"].toInt()));
                _upload_btn->setEnabled(true);
                return;
            }
            int trans_size = json["trans_size"].toInt();
            int total_size = json["total_size"].toInt();
            int last = json["last"].toInt();
            // 进度条按服务器确认收到的字节数推进（而不是本地发的字节数，
            // 这样进度条反映的是"服务器真正落盘了多少"）
            if (total_size > 0) {
                _progress_bar->setValue(trans_size * 100 / total_size);
            }
            if (last == 1) {
                // 最后一块回执：整份文件上传完成
                _status_label->setText(tr("上传完成：%1").arg(json["name"].toString()));
                _upload_btn->setEnabled(true);
            }
            break;
        }
        default:
            qDebug() << "FileDialog not found id [" << msg_id << "] to handle";
            break;
        }
    });

    // 连接资源服务器（协议固定地址，demo 就不做成配置了）
    _socket.connectToHost("127.0.0.1", 9090);
}

// "选择文件"按钮：弹文件对话框，选中后把文件名和大小展示到状态栏
void FileDialog::slot_select_file()
{
    QString path = QFileDialog::getOpenFileName(this, tr("选择要上传的文件"));
    if (path.isEmpty()) {
        return;
    }

    _file_path = path;
    QFileInfo fileInfo(path);
    _status_label->setText(tr("已选择：%1（%2 字节）").arg(fileInfo.fileName()).arg(fileInfo.size()));
    _progress_bar->setValue(0);
    _upload_btn->setEnabled(true);    // 选好了才允许上传
}

// "上传"按钮：照 day31 文档的 on_uploadBtn_clicked 实现
// 流程：只读打开 → 整文件算 md5 → 指针拨回 → 按 32KB 分块 → JSON 组包 → 逐块发送
void FileDialog::slot_upload_file()
{
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        emit sig_net_error(tr("未连接资源服务器，无法上传"));
        return;
    }

    _upload_btn->setEnabled(false);   // 上传期间防重复点击

    QFile file(_file_path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open file:" << file.errorString();
        _status_label->setText(tr("打开文件失败：%1").arg(file.errorString()));
        _upload_btn->setEnabled(true);
        return;
    }

    // 先整文件算 md5：md5 是文件内容的"指纹"，
    // 服务器拿它标识文件，将来断点续传/秒传都靠它判断"传的是不是同一个文件"
    QCryptographicHash hash(QCryptographicHash::Md5);
    qint64 originalPos = file.pos();          // 保存当前文件指针位置
    if (!hash.addData(&file)) {               // addData(&QFile) 会一直读到文件尾
        qWarning() << "Failed to read data from file:" << _file_path;
        _status_label->setText(tr("读取文件失败，无法计算 md5"));
        file.close();
        _upload_btn->setEnabled(true);
        return;
    }
    _file_md5 = hash.result().toHex();        // 32 位十六进制字符串

    QFileInfo fileInfo(_file_path);
    QString fileName = fileInfo.fileName();   // 只发文件名，不带本地路径
    qint64 total_size = fileInfo.size();
    if (total_size <= 0) {
        // 空文件没有可传的分块，直接拒绝，避免服务器收到 last_seq=0 收不了尾
        _status_label->setText(tr("空文件无法上传"));
        file.close();
        _upload_btn->setEnabled(true);
        return;
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
        jsonObj["md5"] = _file_md5;
        jsonObj["name"] = fileName;
        jsonObj["seq"] = static_cast<int>(seq);
        // trans_size：截至本块累计已传字节数 = 本块大小 + 前面整块的固定大小
        jsonObj["trans_size"] = static_cast<int>(buffer.size() + (seq - 1) * MAX_FILE_LEN);
        jsonObj["total_size"] = static_cast<int>(total_size);
        // 用 seq == last_seq 判断最后一块，而不是"读不满 32KB 就是最后一块"：
        // 文件大小恰好是 32KB 整数倍时最后一块也读得满满的，
        // 按"读不满"判断会漏发 last=1，服务器永远等不到收尾信号
        jsonObj["last"] = (seq == last_seq) ? 1 : 0;
        jsonObj["last_seq"] = static_cast<int>(last_seq);
        // 分块数据 base64 编码塞进 JSON：JSON 文本协议没法直接带二进制，
        // 32KB 一块（base64 后约 44KB）资源服务器接受
        jsonObj["data"] = QString::fromLatin1(buffer.toBase64());

        // 每块一个独立的 1003 请求包发出去
        sendMsg(RES_ID_UPLOAD_FILE_REQ, QJsonDocument(jsonObj).toJson(QJsonDocument::Compact));
    }

    file.close();
    _status_label->setText(tr("分块发送完毕（共 %1 块），等待服务器回执...").arg(seq));
}

// 真正发送：组 6 字节包头（2B 消息id + 4B 包体长度，均大端）+ 包体一次写入
void FileDialog::slot_send_msg(quint16 id, QByteArray body)
{
    // 连接异常直接报错返回，不往未连接的 socket 里写
    if (_socket.state() != QAbstractSocket::ConnectedState) {
        emit sig_net_error(tr("断开连接无法发送"));
        return;
    }

    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);       // 和服务器约定的大端一致
    stream << id << static_cast<quint32>(body.size()); // 2B id + 4B 长度 = 6B 包头
    data.append(body);                                 // 接上 JSON 包体

    _socket.write(data);
}

void FileDialog::slot_ready_read()
{
    // TCP 是字节流没有消息边界：先把这次到达的数据全部缓存，再统一切包
    QByteArray data = _socket.readAll();
    _buffer.append(data);
    processData();
}

// 从 _buffer 里循环"切"完整包并按 id 分发：
// 一次 readyRead 可能只有半条消息（半包），也可能粘着好几条（粘包），
// 所以循环解析直到剩下的数据不够拼出一条完整包为止
void FileDialog::processData()
{
    while (_buffer.size() >= FILE_UPLOAD_HEAD_LEN) {
        // 先取出 6 字节包头（2B id + 4B 长度）
        auto head_byte = _buffer.left(FILE_UPLOAD_HEAD_LEN);
        QDataStream stream(head_byte);
        stream.setByteOrder(QDataStream::BigEndian);
        quint16 msg_id;
        stream >> msg_id;
        // 长度字段是 4 字节，必须按 quint32 读：
        // 按 quint16 读会少吞 2 字节，之后的字节边界全错
        quint32 body_length;
        stream >> body_length;

        if (_buffer.size() >= FILE_UPLOAD_HEAD_LEN + qint64(body_length)) {
            // 完整的消息体已经收到：切出来并从缓冲区移除
            QByteArray body = _buffer.mid(FILE_UPLOAD_HEAD_LEN, body_length);
            _buffer = _buffer.mid(FILE_UPLOAD_HEAD_LEN + body_length);

            // 包体是 JSON 文本，解析失败说明流错位或服务器异常，直接断连重来
            QJsonDocument jsonDoc = QJsonDocument::fromJson(body);
            if (jsonDoc.isNull()) {
                qDebug() << "FileDialog failed to create JSON doc.";
                _socket.close();
                return;
            }
            if (!jsonDoc.isObject()) {
                qDebug() << "FileDialog JSON is not an object.";
                _socket.close();
                return;
            }

            // 按 id 分发（进度条/完成提示的 connect 在构造函数里）
            emit sig_logic_process(msg_id, jsonDoc.object());
        } else {
            // 消息体没收全：留在缓冲区，等下一次 readyRead 续上再切
            break;
        }
    }
}
