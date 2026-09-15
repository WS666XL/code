// CServer.cpp —— 监听类 CServer 的实现
#include "CServer.h"
#include"HttpConnection.h"

// 初始化列表：保存 ioc / 创建监听器（绑定 IPv4 指定端口并监听）/ 创建 socket
CServer::CServer(boost::asio::io_context& ioc, unsigned short& port) :_ioc(ioc),
_acceptor(ioc, tcp::endpoint(tcp::v4(), port)), _socket(ioc) {//// 这一行就完成了 socket() + bind() + listen() 三件事

}
