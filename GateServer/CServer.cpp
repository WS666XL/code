// CServer.cpp —— 监听类 CServer 的实现
#include "CServer.h"
#include"HttpConnection.h"
#include"AsioIOServicePool.h"
// 初始化列表：保存 ioc / 创建监听器（绑定 IPv4 指定端口并监听）/ 创建 socket
CServer::CServer(boost::asio::io_context& ioc, unsigned short& port) :_ioc(ioc),
_acceptor(ioc, tcp::endpoint(tcp::v4(), port)){//// 这一行就完成了 socket() + bind() + listen() 三件事

}
void CServer::Start()
{
    auto self = shared_from_this();   // 捕获自身，保证回调期间对象存活
    auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
    std::shared_ptr<HttpConnection> new_con = std::make_shared<HttpConnection>(io_context);
    // 异步 accept：立即返回，有客户端连入时才回调；ec 为错误码
    _acceptor.async_accept(new_con->GetSocket(), [self,new_con](beast::error_code ec) {//防止start函数返回后，CServer对象被析构，导致socket被释放，
        //所以这里使用shared_from_this()获取一个shared_ptr<CServer>，保证CServer对象在异步操作完成前不会被释放
        try {
            //出错则放弃这个连接，继续监听新链接
            if (ec) {
                self->Start();   // 丢弃本次连接，继续监听
                return;
            }

            // 把 _socket 移交给 HttpConnection，由其负责收发；随后继续监听
            new_con->Start();

            self->Start();
        }
        catch (std::exception& exp) {
            // 异常兜底，打印后继续监听
            std::cout << "exception is " << exp.what() << std::endl;
            self->Start();
        }
        });
}