#ifndef FILETCPMGR_H
#define FILETCPMGR_H

#include <QTcpSocket>
#include <QObject>
#include <QMap>
#include <QByteArray>
#include <functional>
#include "singleton.h"
#include "global.h"

/******************************************************************************
 *  文件：filetcpmgr.h
 *  作用：资源服务器（独立监听 127.0.0.1:9090）的长连接管理器，负责文件分块上传
 *  协议：6 字节包头 = 2B 消息ID + 4B 包体长度（均大端），包体是 JSON 文本
 *  与 TcpMgr 的区别：连的是另一台服务器、包头是 6 字节（TcpMgr 是 4 字节），
 *                    所以独立成类，不复用 TcpMgr 的 socket 和解析逻辑
 *  本轮简化：只做 day31 的基础分块上传 + 进度信号，不做断点续传/发送滑窗
 *****************************************************************************/

// ===== 资源服务器消息 ID（独立编号）=====
// 为什么不放进 global.h 的 ReqId 枚举：ReqId 里 1001~1004 已被
// 验证码/注册/重置密码/登录占用，两边是不同服务器、不同协议体系，
// 老师正式版也是独立编号，这里用独立枚举避免撞号。
enum ResReqId {
    RES_ID_TEST_MSG_REQ = 1001,     //链路自测请求：发 {data:"hello"} 验证链路通不通
    RES_ID_TEST_MSG_RSP = 1002,     //链路自测回包：服务器原样带回 data
    RES_ID_UPLOAD_FILE_REQ = 1003,  //文件分块上传请求
    RES_ID_UPLOAD_FILE_RSP = 1004,  //上传进度回执：每落盘一块服务器就回一条
    RES_ID_UPLOAD_HEAD_ICON_REQ = 1031, //头像上传请求（老师消息 ID 1031：带 uid/token，服务端先校验再落盘）
    RES_ID_UPLOAD_HEAD_ICON_RSP = 1032, //头像上传回包：每块落盘后回一条；最后一块落盘+DB更新后才回
    RES_ID_DOWNLOAD_FILE_REQ = 1033,    //文件下载请求（老师消息 ID 1033：按 owner_uid+name 定位，分块回 base64）
    RES_ID_DOWNLOAD_FILE_RSP = 1034,    //文件下载回包：每块一条，is_last=1 表示收完
};

// 包头长度宏以 global.h 为准；这里兜底 #ifndef 保护，
// 防止某个翻译单元先包含本文件、后包含 global.h 时出现重复定义
#ifndef FILE_UPLOAD_HEAD_LEN
#define FILE_UPLOAD_HEAD_LEN 6
#endif

class FileTcpMgr : public QObject, public Singleton<FileTcpMgr>
{
    Q_OBJECT
    friend class Singleton<FileTcpMgr>;   // 只允许单例模板创建对象
public:
    ~FileTcpMgr();
    // 连接资源服务器（host 形如 "127.0.0.1"，资源服务器一般固定 9090）
    void ConnectResServer(const QString& host, quint16 port);
    // 上传本地文件：内部完成 md5 计算 + 按 MAX_FILE_LEN(32KB) 分块 + JSON 组包，
    // 逐块经 sendMsg(1003) 发出；进度通过 sig_upload_progress / sig_upload_done 反馈
    bool UploadFile(QString path);
    // 上传头像（老师消息 1031 链路）：与 UploadFile 同样的分块逻辑，但
    // 请求里额外带 uid/token（服务端首块时用 Redis 里的 utoken_<uid> 校验），
    // 服务端落盘到 output/static/<uid>/head.png 并更新 MySQL user.icon + Redis 缓存；
    // 每块落盘后服务器回 1032，最后一块处理完回的 1032 触发 sig_upload_head_done
    bool UploadHeadIcon(QString path);
    // 下载头像（老师消息 1033 链路）：本地没有对方的头像文件时发起。
    // iconFileName 是服务器文件名（如 head.png），ownerUid 是头像所有者的 uid；
    // 文件保存到 AppData/<ownerUid>/avatars/<iconFileName>，下载完成后发
    // sig_avatar_downloaded(iconFileName)（同一文件名同时只会发一个下载请求）
    void DownloadAvatar(const QString& iconFileName, int ownerUid);
    // 发一条 1033 下载请求（DownloadAvatar 首块和 1034 续块都走它）：
    // 服务端按 output/static/<ownerUid>/<fileName> 定位文件，按 seq 偏移读分块回包
    void SendDownloadReq(const QString& fileName, int ownerUid,
                         const QString& clientPath, int seq);
    // 对外发送入口：把 (消息ID, JSON包体) 经 sig_send_data 信号转给 slot_send_data 真正发出
    void sendMsg(quint16 id, QByteArray body);

private:
    explicit FileTcpMgr(QObject* parent = nullptr);  // 私有构造：在 connect 好所有信号
    void initHandlers();   // 注册"消息ID → 处理函数"的分发表
    void handleMsg(quint16 id, int len, QByteArray data); // 按 id 查分发表再交给对应 lambda

    QTcpSocket _socket;    // 独立于 TcpMgr 的套接字：专门连资源服务器
    QString _host;         // 服务器地址
    quint16 _port;         // 服务器端口
    QByteArray _buffer;    // 接收缓冲区：解决 TCP 粘包/半包
    bool _b_recv_pending;  // 标记：6B 包头已解析但包体没收全，等后续数据续上
    quint16 _message_id;   // 当前这条消息的 ID（包头解析出来的）
    quint32 _message_len;  // 当前这条消息体的长度。注意：协议里长度字段占 4 字节，
                           // 必须按 quint32 解，按 quint16 读会把流解析错位
    // 消息分发表：收到哪种 ID 就交给哪个函数处理。
    // key 用 quint16 而不是 ReqId：资源服务器的 1001~1034 不属于 ReqId 体系
    QMap<quint16, std::function<void(quint16 id, int len, QByteArray data)>> _handlers;

    // 下载中的文件上下文：文件名 → {ownerUid, 本地保存路径, 下一个要请求的 seq}
    // 1034 回包驱动分块续发（is_last=0 时 seq+1 再发 1033），这些状态记在这里
    struct DownloadCtx {
        int owner_uid = 0;
        QString client_path;
        int next_seq = 1;
    };
    QHash<QString, DownloadCtx> _downloading;

public slots:
    // 槽：真正组 6B 包头并写入 socket。外部一律走 sendMsg() 发信号过来
    void slot_send_data(quint16 id, QByteArray body);

signals:
    void sig_res_connect(bool success);   // 资源服务器连接结果（true 成功 / false 失败）
    // 请求发送：内部信号转槽（sig_send_data → slot_send_data）。
    // 好处和 TcpMgr 一致：发送动作统一在 FileTcpMgr 所属线程排队执行，多线程安全
    void sig_send_data(quint16 id, QByteArray body);
    void sig_test_rsp(QString data);      // 1002 链路自测回包带回的 data（当前只打日志）
    void sig_upload_progress(int trans_size, int total_size); // 每块落盘后的进度回执
    void sig_upload_done(QString name);   // last==1：整份文件上传完成
    void sig_upload_head_done(QString name); // 头像上传完成：服务器已落盘+更新 DB/Redis 后回的 1032
    void sig_avatar_downloaded(QString name); // 头像下载完成：本地文件已就绪，界面可重新加载显示
    void sig_connection_closed();         // 连接被服务器断开
};

#endif // FILETCPMGR_H
