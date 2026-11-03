// CServer.h —— 监听类：接受新连接，交给 HttpConnection 处理
#pragma once
#include"const.h"
#include"HttpConnection.h"
// 继承 enable_shared_from_this：异步回调里用 shared_from_this() 捕获自身，防止对象提前析构
class CServer :public std::enable_shared_from_this<CServer>//enable_shared_from_this父类，父类有个智能指针指向CServer
{
public:
    // ioc：事件循环；port：监听端口
    CServer(boost::asio::io_context& ioc, unsigned short& port);

    // 异步接受连接；处理完再调一次自己，形成循环监听
    void Start();
private:
    tcp::acceptor  _acceptor;// 监听器：构造即完成 socket() + bind() + listen() 三件事

    net::io_context& _ioc;   // 事件循环的引用
};
