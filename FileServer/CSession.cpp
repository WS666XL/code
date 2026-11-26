#include "CSession.h"
#include "Logger.h"
#include "CServer.h"
#include <iostream>
#include <sstream>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "LogicSystem.h"
#include <netinet/tcp.h>   // TCP_KEEPIDLE/KEEPINTVL/KEEPCNT（keepalive 细粒度参数）
#include <functional>      // std::hash：按 session 哈希选逻辑线程

CSession::CSession(boost::asio::io_context& io_context, CServer* server):
	_socket(io_context), _server(server), _b_close(false),_b_head_parse(false),_user_uid(0) {
	boost::uuids::uuid  a_uuid = boost::uuids::random_generator()();
	_session_id = boost::uuids::to_string(a_uuid);
	_recv_head_node = make_shared<MsgNode>(HEAD_TOTAL_LEN);
}
CSession::~CSession() {
	LLOG_INFO << "~CSession destruct" << endl;
}

tcp::socket& CSession::GetSocket() {
	return _socket;
}


std::string& CSession::GetSessionId() {
	return _session_id;
}

void CSession::SetUserId(int uid)
{
	_user_uid = uid;
}

int CSession::GetUserId()
{
	return _user_uid;
}

void CSession::Start(){
	// 开启 TCP keepalive：60 秒空闲后开始探测，每 10 秒一次，3 次失败判死(~90秒)。
	// 作用有二：① 探测包会持续刷新客户端侧 NAT 的映射表项，防止空闲连接被路由器
	// 回收成"僵尸连接"（客户端还在发但包永远到不了服务端）；② 服务端能及时清理
	// 已断开的死会话，不占连接资源。
	boost::system::error_code ec;
	_socket.set_option(boost::asio::socket_base::keep_alive(true), ec);
	int idle = 60, intv = 10, cnt = 3;
	setsockopt(_socket.native_handle(), IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
	setsockopt(_socket.native_handle(), IPPROTO_TCP, TCP_KEEPINTVL, &intv, sizeof(intv));
	setsockopt(_socket.native_handle(), IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
	AsyncReadHead(HEAD_TOTAL_LEN);
}
void CSession::AsyncReadHead(int total_len)
{
	auto self = shared_from_this();
	asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				LLOG_INFO << "handle read failed, error is " << ec.message() << endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			if (bytes_transfered < HEAD_TOTAL_LEN) {
				LLOG_INFO << "read length not match, read [" << bytes_transfered << "] , total ["
					<< HEAD_TOTAL_LEN << "]" << endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			_recv_head_node->Clear();
			memcpy(_recv_head_node->_data, _data, bytes_transfered);

			//获取头部MSGID数据（2 字节）
			short msg_id = 0;
			memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
			//网络字节序转化为本地字节序
			msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
			LLOG_INFO << "msg_id is " << msg_id << endl;
			if (msg_id < 0) {
				LLOG_INFO << "invalid msg_id is " << msg_id << endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			//获取头部数据长度（4 字节：文件分块包体较大）
			int msg_len = 0;
			memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
			//网络字节序转化为本地字节序
			msg_len = boost::asio::detail::socket_ops::network_to_host_long(msg_len);
			LLOG_INFO << "msg_len is " << msg_len << endl;

			//长度非法直接断开，防止恶意负数或超大包
			if (msg_len < 0 || msg_len > MAX_LENGTH) {
				LLOG_INFO << "invalid data length is " << msg_len << endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			_recv_msg_node = make_shared<RecvNode>(msg_len, msg_id);
			AsyncReadBody(msg_len);
		}
		catch (std::exception& e) {
			LLOG_INFO << "Exception code is " << e.what() << endl;
		}
		});
}

void CSession::Send(std::string msg, short msgid) {
	std::lock_guard<std::mutex> lock(_send_lock);
	int send_que_size = _send_que.size();
	if (send_que_size > MAX_SENDQUE) {
		LLOG_INFO << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
		return;
	}

	_send_que.push(make_shared<SendNode>(msg.c_str(), msg.length(), msgid));
	if (send_que_size > 0) {
		return;
	}
	auto& msgnode = _send_que.front();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

void CSession::Send(char* msg, int max_length, short msgid) {
	std::lock_guard<std::mutex> lock(_send_lock);
	int send_que_size = _send_que.size();
	if (send_que_size > MAX_SENDQUE) {
		LLOG_INFO << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
		return;
	}

	_send_que.push(make_shared<SendNode>(msg, max_length, msgid));
	if (send_que_size>0) {
		return;
	}
	auto& msgnode = _send_que.front();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

void CSession::Close() {
	_socket.close();
	_b_close = true;
}

std::shared_ptr<CSession>CSession::SharedSelf() {
	return shared_from_this();
}

void CSession::AsyncReadBody(int total_len)
{
	auto self = shared_from_this();
	asyncReadFull(total_len, [self, this, total_len](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				LLOG_INFO << "handle read failed, error is " << ec.message() << endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			if (bytes_transfered < total_len) {
				LLOG_INFO << "read length not match, read [" << bytes_transfered << "] , total ["
					<< total_len<<"]" << endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			memcpy(_recv_msg_node->_data , _data , bytes_transfered);
			_recv_msg_node->_cur_len += bytes_transfered;
			_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
			//此处将消息投递到逻辑队列中：
			//按 session_id 哈希选一个 LogicWorker，同一连接的消息固定进同一个线程，
			//既保证本连接内消息有序，又让不同连接并行处理
			std::hash<std::string> hash_fn;
			int logic_index = hash_fn(_session_id) % LOGIC_WORKER_COUNT;
			LogicSystem::GetInstance()->PostMsgToQue(make_shared<LogicNode>(shared_from_this(), _recv_msg_node), logic_index);
			//继续监听头部接受事件
			AsyncReadHead(HEAD_TOTAL_LEN);
		}
		catch (std::exception& e) {
			LLOG_INFO << "Exception code is " << e.what() << endl;
		}
		});
}


void CSession::HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self) {
	//增加异常处理
	try {
		if (!error) {
			std::lock_guard<std::mutex> lock(_send_lock);
			_send_que.pop();
			if (!_send_que.empty()) {
				auto& msgnode = _send_que.front();
				boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
					std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_self));
			}
		}
		else {
			LLOG_INFO << "handle write failed, error is " << error.message() << endl;
			Close();
			_server->ClearSession(_session_id);
		}
	}
	catch (std::exception& e) {
		LLOG_ERROR << "Exception code : " << e.what() << endl;
	}

}

//读取完整长度
void CSession::asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler )
{
	::memset(_data, 0, MAX_LENGTH);
	asyncReadLen(0, maxLength, handler);
}

//读取指定字节数
void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len,
	std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
	auto self = shared_from_this();
	_socket.async_read_some(boost::asio::buffer(_data + read_len, total_len-read_len),
		[read_len, total_len, handler, self](const boost::system::error_code& ec, std::size_t  bytesTransfered) {
			if (ec) {
				// 出现错误，调用回调函数
				handler(ec, read_len + bytesTransfered);
				return;
			}

			if (read_len + bytesTransfered >= total_len) {
				//长度够了就调用回调函数
				handler(ec, read_len + bytesTransfered);
				return;
			}

			// 没有错误，且长度不足则继续读取
			self->asyncReadLen(read_len + bytesTransfered, total_len, handler);
	});
}

LogicNode::LogicNode(shared_ptr<CSession>  session,
	shared_ptr<RecvNode> recvnode):_session(session),_recvnode(recvnode) {

}
