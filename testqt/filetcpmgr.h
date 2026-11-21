#ifndef FILETCPMGR_H
#define FILETCPMGR_H

#include <QTcpSocket>
#include <QObject>
#include <QMap>
#include <QQueue>
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
 *  day38 断点续传：
 *    上传：1003/1031 也改成"收一条回执再发下一块"，聊天图片/文件（1037）沿用拥塞窗口；
 *          每块进度由服务端写 Redis，暂停后点继续先发 1005 问服务器传到哪（1006 回包），
 *          客户端 seek 到该偏移继续发。
 *    下载：1033/1034 按 seq 请求；本地 .tmp 已有半截文件时从对应 seq 续下，
 *          暂停后不再请求下一块，继续时接着 next_seq 请求。
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
    RES_ID_SYNC_FILE_REQ = 1005,    //断点续传：同步服务器已收到的块序号/字节数
    RES_ID_SYNC_FILE_RSP = 1006,    //同步进度回包（FileNotExists 表示进度过期，从头传）
    RES_ID_UPLOAD_HEAD_ICON_REQ = 1031, //头像上传请求（老师消息 ID 1031：带 uid/token，服务端先校验再落盘）
    RES_ID_UPLOAD_HEAD_ICON_RSP = 1032, //头像上传回包：每块落盘后回一条；最后一块落盘+DB更新后才回
    RES_ID_DOWNLOAD_FILE_REQ = 1033,    //文件下载请求（老师消息 ID 1033：按 owner_uid+name 定位，分块回 base64）
    RES_ID_DOWNLOAD_FILE_RSP = 1034,    //文件下载回包：每块一条，is_last=1 表示收完
    RES_ID_IMG_CHAT_UPLOAD_REQ = 1037,  //上传聊天图片资源（老师消息 ID 1037：带 message_id/sender/receiver）
    RES_ID_IMG_CHAT_UPLOAD_RSP = 1038,  //上传聊天图片资源回包：每块落盘后回一条
};

// 资源服务器错误码：只列客户端需要分支处理的（其余错误统一按失败处理）
enum ResErrorCode {
    RES_SUCCESS = 0,
    RES_FILE_NOT_EXISTS = 1012,   //续传进度不存在（Redis 过期/已完成清理）→ 从 seq=1 重传
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
    // sig_file_downloaded(iconFileName)（同一文件名同时只会发一个下载请求）
    void DownloadAvatar(const QString& iconFileName, int ownerUid);
    // 下载聊天图片（老师消息 1033 链路的图片分支）：
    // 图片服务器落盘在 output/static/<senderUid>/<imgName>，本地缓存到
    // AppData/<我的uid>/chatimg/<senderUid>/<imgName>；完成后发 sig_file_downloaded
    void DownloadChatImage(const QString& imgName, int senderUid);
    // 下载聊天文件（day41：与图片同一条 1033 通道）：
    // 临时文件放 AppData/.../chatfile/<sender>/<unique_name>.tmp 保证断点续传，
    // 下载完成后另存为“下载/qingxun/原始文件名”，并记录本地路径索引
    void DownloadChatFile(const QString& fileName, int senderUid,
                          const QString& originalName = QString());
    // 已下载文件的本地路径（unique_name → 实际保存路径）。
    // 重新登录/切换会话时优先用它找文件，避免重复下载
    QString LookupSavedPath(const QString& uniqueName) const;
    // 上传聊天图片/文件（1037 链路）：1035 拿到 message_id 后调用。
    // 按唯一文件名从 UserMgr 取回 MsgInfo，进入拥塞窗口发送队列：
    // 窗口内最多 MAX_CWND_SIZE 块在途，每收到一条 1038 回执放行下一块
    bool UploadChatImage(const QString& uniqueName);
    // 发一条 1033 下载请求（DownloadAvatar 首块和 1034 续块都走它）：
    // 服务端按 output/static/<ownerUid>/<fileName> 定位文件，按 seq 偏移读分块回包
    void SendDownloadReq(const QString& fileName, int ownerUid,
                         const QString& clientPath, int seq);
    // ===== day38 断点续传：暂停/继续（气泡点按钮时调）=====
    // 上传暂停：只置状态，已发出的块照常等回执，但不再补发新块
    void PauseUpload(const QString& name);
    // 上传继续：先发 1005 同步服务器进度，1006 回来后从断点继续
    void ResumeUpload(const QString& name);
    // 下载暂停：不再请求下一块（本地 .tmp 保留）
    void PauseDownload(const QString& name);
    // 下载继续：从 next_seq 接着请求（若已有在途请求则等它回来再续）
    void ResumeDownload(const QString& name);
    // 查询某个下载是否处于暂停态：会话切走再切回来时，气泡据此恢复"已暂停"而不是自动续下
    bool IsDownloadPaused(const QString& name);
    // 对外发送入口：把 (消息ID, JSON包体) 经 sig_send_data 信号转给 slot_send_data 真正发出
    void sendMsg(quint16 id, QByteArray body);

private:
    explicit FileTcpMgr(QObject* parent = nullptr);  // 私有构造：在 connect 好所有信号
    void initHandlers();   // 注册"消息ID → 处理函数"的分发表
    void handleMsg(quint16 id, int len, QByteArray data); // 按 id 查分发表再交给对应 lambda
    // 按拥塞窗口把文件从"已发位置"继续往后发，直到窗口满或文件发完
    void BatchSend(std::shared_ptr<MsgInfo> msg_info);
    // 当前文件传完后启动队列里的下一个上传（窗口是整条连接共用的，
    // 同一时刻只让一个文件占窗口，避免多个文件互相抢窗口导致谁都传不完）
    void StartNextUpload();
    // 1003/1031 的简单回执驱动上传：一次只发一块，收到回执再发下一块。
    // 与 1037 的拥塞窗口不同，这两个链路数据量小，不需要多块并发
    struct SimpleUploadCtx {
        QString path;          //本地文件路径
        QString name;          //文件名
        QString md5;           //进度 key（服务端 uid|md5）
        qint64 total_size = 0; //总字节数
        qint64 max_seq = 0;    //总块数
        qint64 next_seq = 1;   //下一块要发的 seq
        qint64 last_confirmed_seq = 0; //服务器已确认的 seq
        short req_id = 0;      //1003 或 1031
    };
    QHash<QString, SimpleUploadCtx> _simple_uploads;  //key=md5
    bool StartSimpleUpload(const QString& path, short req_id);
    void SendNextSimpleChunk(const QString& md5);
    // 1005 同步上传进度：请求服务器返回 seq/trans_size
    void SendSyncUploadReq(const QString& md5, const QString& name);
    void FinishChatUpload(std::shared_ptr<MsgInfo> msg_info, bool success);

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

    // 下载中的文件上下文：本地保存路径 → 下载状态
    // 1034 回包驱动分块续发（is_last=0 时 seq+1 再发 1033），这些状态记在这里。
    // key 用 client_path 而不是文件名：不同用户的头像都叫 head.png，
    // 按文件名当 key 会让第二个用户的下载被"去重"直接丢掉，头像永远不更新
    struct DownloadCtx {
        int owner_uid = 0;
        QString name;          //服务器文件名（暂停/继续按它找回上下文）
        QString client_path;   //临时文件路径（UUID，断点续传用）
        QString final_path;    //最终保存路径（聊天文件=下载目录+原始文件名；图片/头像为空）
        int next_seq = 1;      //下一块要请求的 seq
        bool in_flight = false;//已发出 1033、等 1034 回包
        bool paused = false;   //暂停：回包到达后不再请求下一块
        bool resume_allowed = false; //是否允许从 .tmp 断点续传（聊天的唯一文件名才允许；
                                     //头像 head.png 会被后续新头像复用，半截 .tmp 不能续）
        bool resume_tried = false;   //已尝试过续传，服务器拒绝后不再重试，避免死循环
    };
    QHash<QString, DownloadCtx> _downloading;

    // 按名字找下载上下文（聊天图片/文件唯一名；头像不参与暂停，同名也不会误伤）
    DownloadCtx* FindDownloadCtx(const QString& name);

    // ===== 聊天文件本地路径索引 =====
    // AppData/<uid>/file_index.ini：unique_name → 下载后的实际路径
    QString FileIndexPath() const;
    void RememberSavedPath(const QString& uniqueName, const QString& path);
    // 中文"下载"目录下的 qingxun 子目录；目录不存在会自动创建
    QString ResolveDownloadDir() const;
    // 同名文件自动加 (1)(2)，避免覆盖用户已有文件
    QString MakeUniqueFinalPath(const QString& dir, const QString& fileName) const;

    // ===== 上传拥塞窗口（day40）=====
    int _cwnd_size = 0;                 // 整条连接上"已发未回 1038"的分块数
    QString _active_upload;             // 当前占用窗口的 unique_name（空=空闲）
    QQueue<QString> _pending_uploads;   // 排队等窗口的 unique_name

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
    // 聊天图片/文件上传进度：name=unique_name，trans/total 为字节数。
    // ChatPage 按 name 找到对应气泡刷进度条（服务端回执驱动，逗号分隔参数无歧义）
    void sig_chat_upload_progress(QString name, qint64 trans_size, qint64 total_size);
    // 聊天资源下载进度：1034 每块带回 current_size/total_size，气泡刷下载进度条
    void sig_chat_download_progress(QString name, qint64 current_size, qint64 total_size);
    // 聊天资源上传失败（token 失效/进度丢失且无法续传）：气泡切失败态，可重发
    void sig_chat_upload_failed(QString name);
    void sig_upload_done(QString name);   // last==1：整份文件上传完成
    void sig_upload_head_done(QString name); // 头像上传完成：服务器已落盘+更新 DB/Redis 后回的 1032
    void sig_upload_head_failed(int err);    // 头像上传失败：1032 带 error（如 token 失效），界面提示用户
    void sig_file_downloaded(QString name); // 1033 下载完成：本地文件已就绪（头像/聊天图片/聊天文件共用）
    void sig_download_failed(QString name); // 1033 下载失败（文件不存在/登录态失效），界面恢复可重试状态
    void sig_connection_closed();         // 连接被服务器断开
};

#endif // FILETCPMGR_H
