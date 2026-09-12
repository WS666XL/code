// ============================================================================
// CServer.cpp —— 监听类 CServer 的具体实现
// ============================================================================

#include "CServer.h"
#include"HttpConnection.h"

// ---------- 构造函数（初始化列表在干三件事） ----------
// _ioc(ioc)                                    ：保存外部传进来的事件循环
// _acceptor(ioc, tcp::endpoint(tcp::v4(), port))：创建监听器，绑定 IPv4 的指定端口并开始监听
// _socket(ioc)                                 ：创建一个 socket，准备接收即将到来的连接
CServer::CServer(boost::asio::io_context& ioc, unsigned short& port) :_ioc(ioc),
_acceptor(ioc, tcp::endpoint(tcp::v4(), port)), _socket(ioc) {//// 这一行就完成了 socket() + bind() + listen() 三件事

}

