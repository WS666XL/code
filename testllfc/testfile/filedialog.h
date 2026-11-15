#ifndef FILEDIALOG_H
#define FILEDIALOG_H

#include <QDialog>
#include <QTcpSocket>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QByteArray>
#include <QJsonObject>
#include <functional>

/******************************************************************************
 *  文件：filedialog.h（独立联调 demo，纯代码 UI，不用 .ui 文件）
 *  作用：对着 ResourceServer(127.0.0.1:9090) 手工验证文件分块上传协议
 *  协议：6 字节包头 = 2B 消息ID + 4B 包体长度（均大端），包体是 JSON 文本
 *  实现：照 day31 文档的 TcpClient 写法 —— sig_send_msg → slot_send_msg
 *        组 6B 头发送，readyRead 解包后按 1004 回执推进进度条
 *  说明：demo 独立编译运行，协议常量在这里自带，不 include ../testqt 的头文件
 *****************************************************************************/

// ===== 资源服务器消息 ID（与正式客户端 filetcpmgr 同一套协议，独立编号）=====
enum DemoResReqId {
    RES_ID_TEST_MSG_REQ = 1001,     //链路自测请求
    RES_ID_TEST_MSG_RSP = 1002,     //链路自测回包
    RES_ID_UPLOAD_FILE_REQ = 1003,  //文件分块上传请求
    RES_ID_UPLOAD_FILE_RSP = 1004,  //上传进度回执
};

// 6 字节包头 = 2B id + 4B 长度；每块最大 32KB（与主工程 global.h 的宏同值）
#define FILE_UPLOAD_HEAD_LEN 6
#define MAX_FILE_LEN (1024*32)

class FileDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FileDialog(QWidget* parent = nullptr);

private:
    void processData();   // 从 _buffer 里循环"切"完整包并按 id 分发（解决粘包/半包）
    // 对外发送入口：不直接 write，而是 emit sig_send_msg 转给 slot_send_msg，
    // 与 day31 文档的 TcpClient::sendMsg 同款做法
    void sendMsg(quint16 id, QByteArray body) { emit sig_send_msg(id, body); }

    // ---- 界面控件（纯代码构建）----
    QPushButton* _select_btn;    // 选择文件
    QPushButton* _upload_btn;    // 上传（未选文件前置灰）
    QProgressBar* _progress_bar; // 上传进度条
    QLabel* _status_label;       // 状态文字：连接状态/所选文件/上传结果

    // ---- 上传状态 ----
    QString _file_path;          // 本地文件全路径
    QString _file_md5;           // 整文件 md5：给服务器做文件指纹（断点续传预留）

    // ---- 网络收发 ----
    QTcpSocket _socket;          // 连资源服务器 127.0.0.1:9090
    QByteArray _buffer;          // 接收缓冲区：解决 TCP 粘包/半包
    bool _b_recv_pending;        // 包头已解析但包体没收全的挂起标记
    quint16 _message_id;         // 当前消息 ID
    quint32 _message_len;        // 当前包体长度（4 字节字段，必须按 quint32 解）

private slots:
    void slot_select_file();     // "选择文件"按钮：弹 QFileDialog
    void slot_upload_file();     // "上传"按钮：md5 → 分块 → 逐块发 1003
    void slot_send_msg(quint16 id, QByteArray body); // 真正组 6B 包头并 write
    void slot_ready_read();      // 收数据 → 追加缓冲 → processData 解包

signals:
    void sig_send_msg(quint16 id, QByteArray body); // 发送请求信号（转槽，保证线程安全）
    void sig_net_error(QString err);                // 网络异常提示（更新状态文字）
    void sig_logic_process(quint16 msg_id, QJsonObject json); // 解出完整包后按 id 分发
};

#endif // FILEDIALOG_H
