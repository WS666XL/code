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
    void slot_send_data(ReqId reqId, QString data);

signals:
    void sig_con_success(bool bsuccess);        // 连接结果（true 成功 / false 失败）
    void sig_send_data(ReqId reqId, QString data);// 请求发送：界面 emit 它，内部再转成 slot 真正发出
    void sig_swich_chatdlg();                   // 聊天服务器登录成功：通知切到聊天界面
    void sig_login_failed(int);                 // 聊天服务器登录失败：参数是 ErrorCodes 错误码
    void sig_connection_closed();               // 连接被服务器断开

    void sig_user_search(std::shared_ptr<SearchInfo>); // 搜索用户的结果，用 shared_ptr 传，避免拷贝

    void sig_friend_apply(std::shared_ptr<AddFriendApply>);
    void sig_add_auth_friend(std::shared_ptr<AuthInfo>);
    void sig_auth_rsp(std::shared_ptr<AuthRsp>);
};

#endif // TCPMGR_H
