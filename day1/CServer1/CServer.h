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
    void Start()
    {
        auto self = shared_from_this();   // 捕获自身，保证回调期间对象存活

        // 异步 accept：立即返回，有客户端连入时才回调；ec 为错误码
        _acceptor.async_accept(_socket, [self](beast::error_code ec) {//防止start函数返回后，CServer对象被析构，导致socket被释放，
            //所以这里使用shared_from_this()获取一个shared_ptr<CServer>，保证CServer对象在异步操作完成前不会被释放
            try {
                //出错则放弃这个连接，继续监听新链接
                if (ec) {
                    self->Start();   // 丢弃本次连接，继续监听
                    return;
                }

                // 把 _socket 移交给 HttpConnection，由其负责收发；随后继续监听
                std::make_shared<HttpConnection>(std::move(self->_socket))->Start();

                self->Start();
            }
            catch (std::exception& exp) {
                // 异常兜底，打印后继续监听
                std::cout << "exception is " << exp.what() << std::endl;
                self->Start();
            }
            });
    }
private:
    tcp::acceptor  _acceptor;// 监听器：构造即完成 socket() + bind() + listen() 三件事

    net::io_context& _ioc;   // 事件循环的引用

    // 接收新连接用的 socket；用后 move 给 HttpConnection。
    // 不用引用：引用必须初始化时绑定，不能后期改绑
	tcp::socket   _socket;//引用必须在构造函数初始化列表绑定对象，不能后期重新绑定.所以不用引用，直接用对象就行了。
};
