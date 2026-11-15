#ifndef TCPMGR_H
#define TCPMGR_H
#include <QTcpSocket>
#include "singleton.h"
#include "global.h"
#include <functional>
#include <QObject>
#include"userdata.h"

/******************************************************************************
 *  文件：tcpmgr.h
 *  作用：聊天服务器的长连接管理器（TCP 客户端）
 *  协议：自己定的简单协议 —— 包头 4 字节（消息ID + 消息长度，大端），
 *        后面跟着消息体（JSON 文本）
 *  特点：单例，整个程序只有一个和聊天服务器的连接
 *****************************************************************************/
class TcpMgr:public QObject, public Singleton<TcpMgr>,
               public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
    friend class Singleton<TcpMgr>;   // 只允许单例模板创建对象
public:
    ~TcpMgr();
private:
    TcpMgr();                          // 私有构造函数，在里面 connect 好所有信号
    void registerMetaType();           // 注册跨信号槽传递的自定义类型（Qt6 队列信号必需）
    void initHandlers();               // 注册"消息ID → 处理函数"的分发表
    void handleMsg(ReqId id, int len, QByteArray data);//处理消息：按 id 查分发表再交给对应 lambda

    QTcpSocket _socket;                // Qt 的 TCP 套接字，所有收发都走它
    QString _host;                     // 服务器地址
    uint16_t _port;                    // 服务器端口
    QByteArray _buffer;                // 接收缓冲区：解决"包可能拆开到达"的问题
    bool _b_recv_pending;              // 标记：消息体没收全，正在等后续数据
    quint16 _message_id;               // 当前这条消息的 ID（包头解析出来的）
    quint16 _message_len;              // 当前这条消息体的长度（包头解析出来的）
    QMap<ReqId,std::function<void(ReqId id,int len,QByteArray data)>>_handlers;
                                       // 消息分发表：收到哪种 ID 的消息，就交给哪个函数处理

public slots:
    // 槽：连接聊天服务器。参数是登录成功后 HttpMgr 返回的 ServerInfo（地址/端口/token/uid）
    void slot_tcp_connect(ServerInfo);
    // 槽：发送一条消息。reqId 决定包头里的 ID，data 是 JSON 字符串
    void slot_send_data(ReqId reqId, QByteArray dataBytes);
    // 槽：真正关闭 socket（CloseConnection 通过 sig_close 信号触发，保证在TcpMgr所属线程执行）
    void slot_tcp_close();

public:
    // 请求关闭连接：不直接操作 socket，而是发 sig_close 信号转给 slot_tcp_close
    void CloseConnection();

signals:
    void sig_con_success(bool bsuccess);        // 连接结果（true 成功 / false 失败）
    void sig_send_data(ReqId reqId, QByteArray data);// 请求发送：界面 emit 它，内部再转成 slot 真正发出
    void sig_swich_chatdlg();                   // 聊天服务器登录成功：通知切到聊天界面
    void sig_login_failed(int);                 // 聊天服务器登录失败：参数是 ErrorCodes 错误码
    void sig_connection_closed();               // 连接被服务器断开
    void sig_close();                           // 主动关闭连接的内部信号

    // 被踢下线（服务端发来 1021）：MainWindow 收到后弹窗并退回登录界面
    void sig_notify_offline();

    void sig_user_search(std::shared_ptr<SearchInfo>); // 搜索用户的结果，用 shared_ptr 传，避免拷贝

    // 收到好友申请
    void sig_friend_apply(std::shared_ptr<AddFriendApply>);

    // 添加好友认证信息
    void sig_add_auth_friend(std::shared_ptr<AuthInfo>);

    // 好友申请处理结果
    void sig_auth_rsp(std::shared_ptr<AuthRsp>);

    // 文本消息回执/通知：按会话 thread_id 归组，整包列表一次传给界面
    void sig_text_chat_msg(std::vector<std::shared_ptr<TextChatData>> msg_list);

    // 1018 自己发消息的服务端回执（带 thread_id + 服务端分配的 message_id）
    void sig_chat_msg_rsp(int thread_id, std::vector<std::shared_ptr<TextChatData>> msg_list);

    // 1026 聊天线程列表回包：load_more 表示还有没有下一页，last_thread_id 是翻页游标
    void sig_load_chat_thread(bool load_more, int last_thread_id,
        std::vector<std::shared_ptr<ChatThreadInfo>> chat_list);

    // 1028 创建私聊回包：拿到 thread_id 后界面才能建会话条目
    void sig_create_private_chat(int uid, int other_id, int thread_id);

    // day41 群聊：1056 建群回包（success/thread_id/群名/成员数）
    void sig_create_group_rsp(bool success, int thread_id, QString name, int member_count);

    // 1030 历史消息回包：thread_id + 翻页游标 + 消息列表（基类指针，兼容后续图片消息）
    void sig_load_chat_msg(int thread_id, int message_id, bool load_more,
        std::vector<std::shared_ptr<ChatDataBase>> msg_list);

    // ===== day39 图片聊天 =====
    // 1036 回包：自己发的图片消息已入库，带服务端 message_id（界面更新气泡状态，
    // FileTcpMgr 收到信号链后发起 1037 文件上传）
    void sig_chat_img_rsp(int thread_id, std::shared_ptr<ImgChatData> chat_data);
    // 1039 通知：收到别人的图片消息（占位气泡，随后自动下载图片）
    void sig_img_chat_msg(std::shared_ptr<ImgChatData> imgchat);
};

#endif // TCPMGR_H
