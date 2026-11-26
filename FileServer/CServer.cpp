#include "CServer.h"
#include "Logger.h"
#include <iostream>
#include "AsioIOServicePool.h"
CServer::CServer(boost::asio::io_context& io_context, short port):_io_context(io_context), _port(port),
_acceptor(io_context)
{
	// 先 open + reuse_address 再 bind：避免快速重启时端口没释放导致 bind 失败
	boost::system::error_code ec;
	_acceptor.open(tcp::v4(), ec);
	if (ec) {
		LLOG_ERROR << "Server open failed, port " << _port
			<< " err=" << ec.message() << endl;
		throw boost::system::system_error(ec);
	}
	_acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
	if (ec) {
		LLOG_ERROR << "Server set reuse_address failed, port " << _port
			<< " err=" << ec.message() << endl;
	}
	_acceptor.bind(tcp::endpoint(tcp::v4(), port), ec);
	if (ec) {
		LLOG_ERROR << "Server bind failed, port " << _port
			<< " err=" << ec.message() << endl;
		throw boost::system::system_error(ec);
	}
	_acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
	if (ec) {
		LLOG_ERROR << "Server listen failed, port " << _port
			<< " err=" << ec.message() << endl;
		throw boost::system::system_error(ec);
	}
	LLOG_INFO << "Server start success, listen on port : " << _port << endl;
	StartAccept();
}

CServer::~CServer() {
	LLOG_INFO << "Server destruct listen on port : " << _port << endl;
}

void CServer::HandleAccept(shared_ptr<CSession> new_session, const boost::system::error_code& error){
	if (!error) {
		new_session->Start();
		lock_guard<mutex> lock(_mutex);
		_sessions.insert(make_pair(new_session->GetSessionId(), new_session));
	}
	else {
		LLOG_INFO << "session accept failed, error is " << error.message() << endl;
	}

	StartAccept();
}

void CServer::StartAccept() {
	auto &io_context = AsioIOServicePool::GetInstance()->GetIOService();
	shared_ptr<CSession> new_session = make_shared<CSession>(io_context, this);
	_acceptor.async_accept(new_session->GetSocket(), std::bind(&CServer::HandleAccept, this, new_session, placeholders::_1));
}

void CServer::ClearSession(std::string sessions_id) {
	//资源服务器没有 uid→session 映射，只需从会话表移除即可
	{
		lock_guard<mutex> lock(_mutex);
		_sessions.erase(sessions_id);
	}

}