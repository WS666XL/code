#pragma once
#include <boost/asio.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <queue>
#include <mutex>
#include <memory>
#include "const.h"
#include "MsgNode.h"

using namespace std;

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class CServer;
class LogicSystem;

class CSession : public std::enable_shared_from_this<CSession>
{
public:
    CSession(boost::asio::io_context& io_context, CServer* server); // 构造函数，初始化客户端会话
    ~CSession(); // 析构函数
    void SetUserId(int uid);
    int GetUserId();
    tcp::socket& GetSocket(); // 获取客户端的Socket
    std::string& GetSessionId(); // 获取会话唯一标识
    void Start(); // 启动会话，开始接收客户端数据

    void Send(char* msg, short max_length, short msgid); // 发送指定长度的消息
    void Send(std::string msg, short msgid); // 发送字符串消息
    void Close(); // 关闭会话

    std::shared_ptr<CSession> SharedSelf(); // 获取当前对象的智能指针
    void AsyncReadBody(int length); // 异步读取消息正文
    void AsyncReadHead(int total_len); // 异步读取消息头

private:
    // 异步读取指定长度的数据，完成后调用回调函数
    void asyncReadFull(std::size_t maxLength,
        std::function<void(const boost::system::error_code&, std::size_t)> handler);

    // 异步读取指定长度的数据，累计读取到目标长度
    void asyncReadLen(std::size_t read_len, std::size_t total_len,
        std::function<void(const boost::system::error_code&, std::size_t)> handler);

    void HandleWrite(const boost::system::error_code& error,
        std::shared_ptr<CSession> shared_self); // 处理异步发送结果

    tcp::socket _socket; // 与客户端通信的TCP套接字
    std::string _session_id; // 当前会话的唯一标识
    char _data[MAX_LENGTH]; // 接收数据的缓冲区
    CServer* _server; // 指向所属服务器对象
    bool _b_close; // 标记会话是否关闭

    std::queue<shared_ptr<SendNode>> _send_que; // 待发送消息队列
    std::mutex _send_lock; // 保护发送队列的互斥锁

    std::shared_ptr<RecvNode> _recv_msg_node; // 保存接收到的消息正文

    bool _b_head_parse; // 标记消息头是否解析完成

    std::shared_ptr<MsgNode> _recv_head_node; // 保存接收到的消息头
    int _user_uid;
};

class LogicNode {
    friend class LogicSystem;

public:
    LogicNode(shared_ptr<CSession>, shared_ptr<RecvNode>); // 构造逻辑处理节点

private:
    shared_ptr<CSession> _session; // 保存对应的客户端会话
    shared_ptr<RecvNode> _recvnode; // 保存待处理的接收消息
};