#include "LogicSystem.h"
#include "StatusGrpcClient.h"
#include "MysqlMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include"UserMgr.h"
#include"ChatGrpcClient.h"
#include"CServer.h"
#include"utils.h"
using namespace std;

LogicSystem::LogicSystem():_b_stop(false), _p_server(nullptr){
	RegisterCallBacks();
	_worker_thread = std::thread (&LogicSystem::DealMsg, this);
}

// main 里把 CServer 智能指针传进来，踢人时用它清除旧 session
void LogicSystem::SetServer(std::shared_ptr<CServer> pserver) {
	_p_server = pserver;
}

LogicSystem::~LogicSystem(){
	_b_stop = true;
	_consume.notify_one();
	_worker_thread.join();
}

void LogicSystem::PostMsgToQue(shared_ptr <LogicNode> msg) {
	std::unique_lock<std::mutex> unique_lk(_mutex);
	_msg_que.push(msg);
	//由0变为1则发送通知信号
	if (_msg_que.size() == 1) {
		unique_lk.unlock();
		_consume.notify_one();
	}
}

void LogicSystem::DealMsg() {
	for (;;) {
		std::unique_lock<std::mutex> unique_lk(_mutex);
		//判断队列为空则用条件变量阻塞等待，并释放锁
		while (_msg_que.empty() && !_b_stop) {
			_consume.wait(unique_lk);
		}

		//判断是否为关闭状态，把所有逻辑执行完后则退出循环
		if (_b_stop ) {
			while (!_msg_que.empty()) {
				auto msg_node = _msg_que.front();
				cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << endl;
				auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
				if (call_back_iter == _fun_callbacks.end()) {
					_msg_que.pop();
					continue;
				}
				call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
					std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_total_len));//改成_total_len
				_msg_que.pop();
			}
			break;
		}

		//如果没有停服，且说明队列中有数据
		auto msg_node = _msg_que.front();
		cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << endl;
		auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
		if (call_back_iter == _fun_callbacks.end()) {
			_msg_que.pop();
			std::cout << "msg id [" << msg_node->_recvnode->_msg_id << "] handler not found" << std::endl;
			continue;
		}
		call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id, 
			std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_total_len));//改成_total_len
		_msg_que.pop();
	}
}

void LogicSystem::RegisterCallBacks() {
	_fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_SEARCH_USER_REQ] = std::bind(&LogicSystem::SearchInfo, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_ADD_FRIEND_REQ] = std::bind(&LogicSystem::AddFriendApply, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_AUTH_FRIEND_REQ] = std::bind(&LogicSystem::AuthFriendApply, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_TEXT_CHAT_MSG_REQ] = std::bind(&LogicSystem::DealChatTextMsg, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_HEART_BEAT_REQ] = std::bind(&LogicSystem::HeartBeatHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_LOAD_CHAT_THREAD_REQ] = std::bind(&LogicSystem::GetUserThreadsHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_CREATE_PRIVATE_CHAT_REQ] = std::bind(&LogicSystem::CreatePrivateChat, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_LOAD_CHAT_MSG_REQ] = std::bind(&LogicSystem::LoadChatMsg, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_CREATE_GROUP_REQ] = std::bind(&LogicSystem::DealCreateGroup, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
}
bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		userinfo->uid = root["uid"].asInt();
		userinfo->name = root["name"].asString();
		userinfo->pwd = root["pwd"].asString();
		userinfo->email = root["email"].asString();
		userinfo->nick = root["nick"].asString();
		userinfo->desc = root["desc"].asString();
		userinfo->sex = root["sex"].asInt();
		userinfo->icon = root["icon"].asString();
		std::cout << "user login uid is  " << userinfo->uid << " name  is "
			<< userinfo->name << " pwd is " << userinfo->pwd << " email is " << userinfo->email << endl;
	}
	else {
		//redis中没有则查询mysql
		//查询数据库
		std::shared_ptr<UserInfo> user_info = nullptr;
		user_info = MysqlMgr::GetInstance()->GetUser(uid);
		if (user_info == nullptr) {
			return false;
		}

		userinfo = user_info;

		//将数据库内容写入redis缓存
		Json::Value redis_root;
		redis_root["uid"] = uid;
		redis_root["pwd"] = userinfo->pwd;
		redis_root["name"] = userinfo->name;
		redis_root["email"] = userinfo->email;
		redis_root["nick"] = userinfo->nick;
		redis_root["desc"] = userinfo->desc;
		redis_root["sex"] = userinfo->sex;
		redis_root["icon"] = userinfo->icon;
		RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	}
	return true;
}

bool LogicSystem::isPureDigit(const std::string& str)
{
	for (char c : str) {
		if (!std::isdigit(c)) {
			return false;
		}
	}
	return true;
}

void LogicSystem::LoginHandler(shared_ptr<CSession> session, const short &msg_id, const string &msg_data) {
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid = root["uid"].asInt();
	auto token = root["token"].asString();
	std::cout << "user login uid is  " << uid << " user token  is "<<
		token<< endl;
	//从状态服务器获取token匹配是否准确
	/*auto rsp = StatusGrpcClient::GetInstance()->Login(uid, root["token"].asString());
	Json::Value  rtvalue;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
	});*/
	Json::Value  rtvalue;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
		});

	//从redis获取用户token是否正确
	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;
	std::string token_value = "";
	bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
	if (!success) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}
	if (token_value != token) {
		rtvalue["error"] = ErrorCodes::TokenInvalid;
		return;
	}
	rtvalue["error"] = ErrorCodes::Success;
	/*rtvalue["error"] = rsp.error();
	if (rsp.error() != ErrorCodes::Success) {
		return;
	}*/

	std::string base_key = USER_BASE_INFO + uid_str;
	auto user_info = std::make_shared<UserInfo>();
	bool b_base = GetBaseInfo(base_key, uid, user_info);
	if (!b_base) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}
	rtvalue["uid"] = uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;
	//从数据库获取申请列表
	std::vector<std::shared_ptr<ApplyInfo>> apply_list;
	auto b_apply = GetFriendApplyInfo(uid, apply_list);
	if (b_apply) {
		for (auto& apply : apply_list) {
			Json::Value obj;
			obj["name"] = apply->_name;
			obj["uid"] = apply->_uid;
			obj["icon"] = apply->_icon;
			obj["nick"] = apply->_nick;
			obj["sex"] = apply->_sex;
			obj["desc"] = apply->_desc;
			obj["status"] = apply->_status;
			rtvalue["apply_list"].append(obj);
		}
	}

	//获取好友列表
	std::vector<std::shared_ptr<UserInfo>> friend_list;
	bool b_friend_list = GetFriendList(uid, friend_list);
	for (auto& friend_ele : friend_list) {
		Json::Value obj;
		obj["name"] = friend_ele->name;
		obj["uid"] = friend_ele->uid;
		obj["icon"] = friend_ele->icon;
		obj["nick"] = friend_ele->nick;
		obj["sex"] = friend_ele->sex;
		obj["desc"] = friend_ele->desc;
		obj["back"] = friend_ele->back;
		rtvalue["friend_list"].append(obj);
	}


	auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");
	//将登录数量增加
	auto rd_res = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server_name);
	int count = 0;
	if (!rd_res.empty()) {
		count = std::stoi(rd_res);
	}

	count++;
	auto count_str = std::to_string(count);
	RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, count_str);

	//此处添加分布式锁，让"查旧登录→踢人→写新登录"这段流程独占执行，
	//防止两个节点同时给同一 uid 登录时互相漏踢
	{
		//拼接用户 uid 对应的锁 key
		auto lock_key = LOCK_PREFIX + uid_str;
		auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
		//利用defer解锁
		Defer defer2([this, identifier, lock_key]() {
			RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
			});

		//查 Redis 里该 uid 上次登录记录的节点名（uip_1001 -> "chatserver1"）
		std::string uid_ip_value = "";
		auto uid_ip_key = USERIPPREFIX + uid_str;
		bool b_ip = RedisMgr::GetInstance()->Get(uid_ip_key, uid_ip_value);
		std::cout << "kick check: GET " << uid_ip_key << " -> [" << uid_ip_value
			<< "]  self=" << server_name << std::endl;
		//说明用户已经登录了，此处应该踢掉之前的用户登录状态
		if (b_ip) {
			//获取当前服务器名字
			auto& cfg = ConfigMgr::Inst();
			auto self_name = cfg["SelfServer"]["Name"];
			//如果之前登录的节点和当前相同，则直接在本服务器踢掉旧连接
			if (uid_ip_value == self_name) {
				std::cout << "kick: same server, clearing local old session" << std::endl;
				//查找旧有的连接
				auto old_session = UserMgr::GetInstance()->GetSession(uid);

				//此处应该发送踢人消息
				if (old_session) {
					old_session->NotifyOffline(uid);
					//清除旧的连接
					_p_server->ClearSession(old_session->GetSessionId());
					std::cout << "kick: local old session kicked" << std::endl;
				}

			}
			else {
				//如果之前登录在另一台节点，则通过 gRPC 通知那台节点踢掉
				std::cout << "kick: cross server, notifying [" << uid_ip_value << "]" << std::endl;
				KickUserReq kick_req;
				kick_req.set_uid(uid);
				ChatGrpcClient::GetInstance()->NotifyKickUser(uid_ip_value, kick_req);
			}
		}

		//session绑定用户uid
		session->SetUserId(uid);
		//为用户设置登录的节点名字（下次登录靠这个判断"旧登录在哪台节点"）
		std::string  ipkey = USERIPPREFIX + uid_str;
		RedisMgr::GetInstance()->Set(ipkey, server_name);
		//uid和session绑定管理,方便以后踢人操作
		UserMgr::GetInstance()->SetUserSession(uid, session);
		//记录 uid 当前绑定的 session id
		std::string  uid_session_key = USER_SESSION_PREFIX + uid_str;
		RedisMgr::GetInstance()->Set(uid_session_key, session->GetSessionId());
	}

	return;
}

// 心跳：客户端每 10s 发一次 {fromuid:xx}，回 error=0 即可
void LogicSystem::HeartBeatHandler(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data) {
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid = root["fromuid"].asInt();
	std::cout << "receive heart beat msg, uid is " << uid << std::endl;
	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	session->Send(rtvalue.toStyledString(), ID_HEARTBEAT_RSP);
}

// 加载聊天线程列表：请求 {uid, thread_id(上页最后一条，首页传0)}，
// 回 {error, uid, load_more, next_last_id, threads:[{thread_id,type,user1_id,user2_id}]}
void LogicSystem::GetUserThreadsHandler(std::shared_ptr<CSession> session,
	const short& msg_id, const string& msg_data)
{
	//从数据库加载 chat_thread 记录
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid = root["uid"].asInt();
	int last_id = root["thread_id"].asInt();
	std::cout << "get uid  threads  " << uid << std::endl;

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["uid"] = uid;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_LOAD_CHAT_THREAD_RSP);
		});

	std::vector<std::shared_ptr<ChatThreadInfo>> threads;

	int page_size = 10;
	bool load_more = false;
	int next_last_id = 0;
	bool res = GetUserThreads(uid, last_id, page_size, threads, load_more, next_last_id);
	if (!res) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	rtvalue["load_more"] = load_more;
	rtvalue["next_last_id"] = (int)next_last_id;
	//整理 threads 数据写入 json 返回
	for (auto& thread : threads) {
		Json::Value thread_value;
		thread_value["thread_id"] = int(thread->_thread_id);
		thread_value["type"] = thread->_type;
		thread_value["user1_id"] = thread->_user1_id;
		thread_value["user2_id"] = thread->_user2_id;
		//群聊带群名（客户端建群条目显示用；私聊为空串）
		thread_value["group_name"] = thread->_group_name;
		rtvalue["threads"].append(thread_value);
	}
}

bool LogicSystem::GetUserThreads(int64_t userId,
	int64_t lastId,
	int      pageSize,
	std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
	bool& loadMore,
	int& nextLastId)
{
	return MysqlMgr::GetInstance()->GetUserThreads(userId, lastId, pageSize,
		threads, loadMore, nextLastId);
}

// 创建私聊线程：请求 {uid, other_id}，回 {error, uid, other_id, thread_id}
// 线程已存在则直接复用（靠 private_chat 的 user1+user2 唯一键去重）
void LogicSystem::CreatePrivateChat(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid = root["uid"].asInt();
	auto other_id = root["other_id"].asInt();

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["uid"] = uid;
	rtvalue["other_id"] = other_id;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_CREATE_PRIVATE_CHAT_RSP);
		});

	int thread_id = 0;
	bool res = MysqlMgr::GetInstance()->CreatePrivateChat(uid, other_id, thread_id);
	if (!res) {
		rtvalue["error"] = ErrorCodes::CREATE_CHAT_FAILED;
		return;
	}

	rtvalue["thread_id"] = thread_id;
}

// 分页加载历史消息：请求 {thread_id, message_id(上页最后一条，首页传0)}，
// 回 {error, thread_id, last_message_id, load_more, chat_datas:[...]}
void LogicSystem::LoadChatMsg(std::shared_ptr<CSession> session,
	const short& msg_id, const string& msg_data) {

	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto thread_id = root["thread_id"].asInt();
	auto message_id = root["message_id"].asInt();

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["thread_id"] = thread_id;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_LOAD_CHAT_MSG_RSP);
		});

	int page_size = 10;
	std::shared_ptr<PageResult> res = MysqlMgr::GetInstance()->LoadChatMsg(thread_id, message_id, page_size);
	if (!res) {
		rtvalue["error"] = ErrorCodes::LOAD_CHAT_FAILED;
		return;
	}

	rtvalue["last_message_id"] = res->next_cursor;
	rtvalue["load_more"] = res->load_more;
	for (auto& chat : res->messages) {
		Json::Value  chat_data;
		chat_data["sender"] = chat.sender_id;
		chat_data["msg_id"] = chat.message_id;
		chat_data["thread_id"] = chat.thread_id;
		chat_data["unique_id"] = 0;
		chat_data["msg_content"] = chat.content;
		chat_data["chat_time"] = chat.chat_time;
		chat_data["status"] = chat.status;
		chat_data["msg_type"] = chat.msg_type;
		chat_data["receiver"] = chat.recv_id;
		rtvalue["chat_datas"].append(chat_data);
	}
}

void LogicSystem::SearchInfo(shared_ptr<CSession >session, const short& msg_id, const string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid_str = root["uid"].asString();
	std::cout << "user SearchInfo uid is  " << uid_str << endl;

	Json::Value  rtvalue;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_SEARCH_USER_RSP);
		});

	bool b_digit = isPureDigit(uid_str);
	if (b_digit) {
		GetUserByUid(uid_str, rtvalue);
	}
	else {
		GetUserByName(uid_str, rtvalue);
	}
	return;
}
void LogicSystem::AddFriendApply(shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid = root["uid"].asInt();
	auto applyname = root["applyname"].asString();
	auto bakname = root["bakname"].asString();
	auto touid = root["touid"].asInt();
	std::cout << "user login uid is  " << uid << " applyname  is "
		<< applyname << " bakname is " << bakname << " touid is " << touid << endl;

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_ADD_FRIEND_RSP);
		});

	//先更新数据库
	MysqlMgr::GetInstance()->AddFriendApply(uid, touid);

	//查询redis 查找touid对应的server ip  对方可能和你在同一个server 也可能在不同的server
	auto to_str = std::to_string(touid);
	auto to_ip_key = USERIPPREFIX + to_str;
	std::string to_ip_value = "";
	bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
	if (!b_ip) {
		return;
	}
	std::string base_key = USER_BASE_INFO + std::to_string(uid);
	auto apply_info = std::make_shared<UserInfo>();
	bool b_info = GetBaseInfo(base_key, uid, apply_info);//提前查一下，把完整的信息交给rpc

	auto& cfg = ConfigMgr::Inst();
	auto self_name = cfg["SelfServer"]["Name"];//对方可能和你在同一个server 也可能在不同的server
	//直接通知对方有申请消息
	if (to_ip_value == self_name) {
		auto session = UserMgr::GetInstance()->GetSession(touid);
		if (session) {
			//在内存中则直接发送通知对方
			Json::Value  notify;
			notify["error"] = ErrorCodes::Success;
			notify["applyuid"] = uid;
			notify["name"] = applyname;
			notify["desc"] = "";
			if (b_info)
			{
				notify["icon"] = apply_info->icon;
				notify["sex"] = apply_info->sex;
				notify["nick"] = apply_info->nick;
			}
			
			std::string return_str = notify.toStyledString();
			session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
		}

		return;
	}

	AddFriendReq add_req;
	add_req.set_applyuid(uid);
	add_req.set_touid(touid);
	add_req.set_name(applyname);
	add_req.set_desc("");
	if (b_info) {
		add_req.set_icon(apply_info->icon);
		add_req.set_sex(apply_info->sex);
		add_req.set_nick(apply_info->nick);
	}

	//发送通知
	ChatGrpcClient::GetInstance()->NotifyAddFriend(to_ip_value, add_req);
}
void LogicSystem::AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid = root["fromuid"].asInt();
	auto touid = root["touid"].asInt();
	auto back_name = root["back"].asString();
	std::cout << "from " << uid << " auth friend to " << touid << std::endl;

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	auto user_info = std::make_shared<UserInfo>();

	std::string base_key = USER_BASE_INFO + std::to_string(touid);
	bool b_info = GetBaseInfo(base_key, touid, user_info);
	if (b_info) {
		rtvalue["name"] = user_info->name;
		rtvalue["nick"] = user_info->nick;
		rtvalue["icon"] = user_info->icon;
		rtvalue["sex"] = user_info->sex;
		rtvalue["uid"] = touid;
	}
	else {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;//查不到回去
	}


	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_AUTH_FRIEND_RSP);
		});

	//先更新数据库
	MysqlMgr::GetInstance()->AuthFriendApply(uid, touid);

	//更新数据库添加好友
	MysqlMgr::GetInstance()->AddFriend(uid, touid, back_name);

	//查询redis 查找touid对应的server ip
	auto to_str = std::to_string(touid);
	auto to_ip_key = USERIPPREFIX + to_str;
	std::string to_ip_value = "";
	bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
	if (!b_ip) {
		return;
	}

	auto& cfg = ConfigMgr::Inst();
	auto self_name = cfg["SelfServer"]["Name"];
	//直接通知对方有认证通过消息
	if (to_ip_value == self_name) {
		auto session = UserMgr::GetInstance()->GetSession(touid);
		if (session) {
			//在内存中则直接发送通知对方
			Json::Value  notify;
			notify["error"] = ErrorCodes::Success;
			notify["fromuid"] = uid;
			notify["touid"] = touid;
			std::string base_key = USER_BASE_INFO + std::to_string(uid);
			auto user_info = std::make_shared<UserInfo>();
			bool b_info = GetBaseInfo(base_key, uid, user_info);
			if (b_info) {
				notify["name"] = user_info->name;
				notify["nick"] = user_info->nick;
				notify["icon"] = user_info->icon;
				notify["sex"] = user_info->sex;
			}
			else {
				notify["error"] = ErrorCodes::UidInvalid;
			}


			std::string return_str = notify.toStyledString();
			session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
		}

		return;
	}


	AuthFriendReq auth_req;
	auth_req.set_fromuid(uid);
	auth_req.set_touid(touid);

	//发送通知
	ChatGrpcClient::GetInstance()->NotifyAuthFriend(to_ip_value, auth_req);
}
void LogicSystem::GetUserByUid(std::string uid_str, Json::Value& rtvalue)
{
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = USER_BASE_INFO + uid_str;

	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto pwd = root["pwd"].asString();
		auto email = root["email"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		auto icon = root["icon"].asString();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " pwd is " << pwd << " email is " << email << " icon is " << endl;

		rtvalue["uid"] = uid;
		rtvalue["pwd"] = pwd;
		rtvalue["name"] = name;
		rtvalue["email"] = email;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		rtvalue["icon"] = icon;
		return;
	}

	auto uid = std::stoi(uid_str);
	//redis中没有则查询mysql
	//查询数据库
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(uid);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	//将数据库内容写入redis缓存
	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;
	redis_root["icon"] = user_info->icon;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

	//返回数据
	rtvalue["uid"] = user_info->uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;
}

void LogicSystem::GetUserByName(std::string name, Json::Value& rtvalue)
{
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = NAME_INFO + name;

	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto pwd = root["pwd"].asString();
		auto email = root["email"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		auto icon = root["icon"].asString();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " pwd is " << pwd << " email is " << email << endl;

		rtvalue["uid"] = uid;
		rtvalue["pwd"] = pwd;
		rtvalue["name"] = name;
		rtvalue["email"] = email;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		rtvalue["icon"] = icon;
		return;
	}

	//redis中没有则查询mysql
	//查询数据库
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(name);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	//将数据库内容写入redis缓存
	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;
	redis_root["icon"] = user_info->icon;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

	//返回数据
	rtvalue["uid"] = user_info->uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;
}

bool LogicSystem::GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& list)
{
	//从mysql获取好友申请列表
	return MysqlMgr::GetInstance()->GetApplyList(to_uid, list, 0, 10);
}

bool LogicSystem::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_list)
{
	//从mysql获取好友列表
	return MysqlMgr::GetInstance()->GetFriendList(self_id, user_list);
}

void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid = root["fromuid"].asInt();
	auto touid = root["touid"].asInt();
	//会话形式（0=私聊 1=群聊；旧客户端不带该字段缺省为私聊）：
	//群消息 touid=0 无单一接收者，通知时遍历群成员逐个推
	auto form_type = root["form_type"].asInt();

	const Json::Value  arrays = root["text_array"];

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;

	rtvalue["fromuid"] = uid;
	rtvalue["touid"] = touid;
	//聊天线程 id：客户端打开会话前先通过 ID_CREATE_PRIVATE_CHAT_REQ 拿到
	auto thread_id = root["thread_id"].asInt();
	rtvalue["thread_id"] = thread_id;

	//把每条消息组织成 ChatMessage，统一落库
	std::vector<std::shared_ptr<ChatMessage>> chat_datas;
	auto timestamp = getCurrentTimestamp();
	for (const auto& txt_obj : arrays) {
		auto content = txt_obj["content"].asString();
		auto unique_id = txt_obj["unique_id"].asString();
		std::cout << "content is " << content << std::endl;
		std::cout << "unique_id is " << unique_id << std::endl;
		auto chat_msg = std::make_shared<ChatMessage>();
		chat_msg->chat_time = timestamp;
		chat_msg->sender_id = uid;
		chat_msg->recv_id = touid;
		chat_msg->unique_id = unique_id;
		chat_msg->thread_id = thread_id;
		chat_msg->content = content;
		chat_msg->status = 2;
		chat_msg->msg_type = int(ChatMsgType::TEXT);
		chat_datas.push_back(chat_msg);
	}

	//插入数据库（回填自增 message_id，客户端靠 unique_id 匹配回执）
	MysqlMgr::GetInstance()->AddChatMsg(chat_datas);

	for (const auto& chat_data : chat_datas) {
		Json::Value  chat_msg;
		chat_msg["message_id"] = chat_data->message_id;
		chat_msg["unique_id"] = chat_data->unique_id;
		chat_msg["content"] = chat_data->content;
		chat_msg["status"] = chat_data->status;
		chat_msg["chat_time"] = chat_data->chat_time;
		rtvalue["chat_datas"].append(chat_msg);
	}

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_TEXT_CHAT_MSG_RSP);
		});

	auto& cfg = ConfigMgr::Inst();
	auto self_name = cfg["SelfServer"]["Name"];

	// ===== day41 群消息通知：遍历群成员逐个推 1019（发送者除外）=====
	// 通知 JSON 与私聊完全同构（客户端按 thread_id 定位会话，touid 对群聊无意义），
	// 气泡发送者由每条消息的 fromuid 区分；跨节点的成员复用现有 NotifyTextChatMsg
	if (form_type == int(ChatFormType::GROUP)) {
		std::vector<int> member_uids;
		if (!MysqlMgr::GetInstance()->GetGroupMembers(thread_id, member_uids)) {
			return;
		}

		for (auto m_uid : member_uids) {
			if (m_uid == uid) {
				continue;   //发送者自己不推（回包已确认）
			}
			auto to_str = std::to_string(m_uid);
			auto to_ip_key = USERIPPREFIX + to_str;
			std::string to_ip_value = "";
			if (!RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value)) {
				continue;   //成员不在线，跳过（消息已落库，登录后走历史加载）
			}

			if (to_ip_value == self_name) {
				auto m_session = UserMgr::GetInstance()->GetSession(m_uid);
				if (m_session) {
					std::string return_str = rtvalue.toStyledString();
					m_session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
				}
				continue;
			}

			//成员在别的节点：gRPC 通知那台节点转发（touid 填该成员 uid）
			TextChatMsgReq text_msg_req;
			text_msg_req.set_fromuid(uid);
			text_msg_req.set_touid(m_uid);
			text_msg_req.set_thread_id(thread_id);
			for (const auto& chat_data : chat_datas) {
				auto* text_msg = text_msg_req.add_textmsgs();
				text_msg->set_unique_id(chat_data->unique_id);
				text_msg->set_msgcontent(chat_data->content);
				text_msg->set_msg_id(chat_data->message_id);
				text_msg->set_chat_time(chat_data->chat_time);
			}
			ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, text_msg_req, rtvalue);
		}
		return;
	}

	// ===== 私聊通知（现有逻辑）：查接收者所在节点 =====
	//查询redis 查找touid对应的server ip
	auto to_str = std::to_string(touid);
	auto to_ip_key = USERIPPREFIX + to_str;
	std::string to_ip_value = "";
	bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
	if (!b_ip) {
		return;
	}

	//对方在本节点则直接发通知
	if (to_ip_value == self_name) {
		auto session = UserMgr::GetInstance()->GetSession(touid);
		if (session) {
			//在内存中则直接发送通知对方
			std::string return_str = rtvalue.toStyledString();
			session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
		}

		return;
	}

	//对方在别的节点，通过 gRPC 通知那台节点转发给对方
	TextChatMsgReq text_msg_req;
	text_msg_req.set_fromuid(uid);
	text_msg_req.set_touid(touid);
	text_msg_req.set_thread_id(thread_id);
	for (const auto& chat_data : chat_datas) {
		auto* text_msg = text_msg_req.add_textmsgs();
		text_msg->set_unique_id(chat_data->unique_id);
		text_msg->set_msgcontent(chat_data->content);
		text_msg->set_msg_id(chat_data->message_id);
		text_msg->set_chat_time(chat_data->chat_time);
	}

	//发送通知
	ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, text_msg_req, rtvalue);
}

// 1035 图片聊天消息：与文本消息"先入库再转发"不同，图片消息只入库+回包，
// 不在这里通知对方——收图通知（1039）由资源服务器在图片文件全部落盘后
// 通过 gRPC NotifyChatImgMsg 反向触发（老师版设计：保证对方收到通知时图片可下载）。
// 入库 status=0（资源未传完），文件传完后由 ResourceServer 的 UpdateUploadStatus 置 2
void LogicSystem::DealChatImgMsg(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid = root["fromuid"].asInt();
	auto touid = root["touid"].asInt();
	auto thread_id = root["thread_id"].asInt();
	auto md5 = root["md5"].asString();
	auto unique_name = root["name"].asString();
	auto unique_id = root["unique_id"].asString();
	auto original_name = root["file_name"].asString();   //原始文件名（文件消息才有）
	//消息类型（1=图片 3=文件）：图片与文件共用 1035/1037/1039 这套链路，
	//服务端只按类型入库，落盘/通知逻辑完全复用
	auto msg_type = root["msg_type"].asInt();
	if (msg_type != int(ChatMsgType::PIC) && msg_type != int(ChatMsgType::FILE)) {
		msg_type = int(ChatMsgType::PIC);   //非法值兜底为图片
	}

	Json::Value rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["fromuid"] = uid;
	rtvalue["touid"] = touid;
	rtvalue["thread_id"] = thread_id;
	rtvalue["md5"] = md5;
	rtvalue["unique_name"] = unique_name;
	rtvalue["unique_id"] = unique_id;

	auto timestamp = getCurrentTimestamp();
	rtvalue["chat_time"] = timestamp;

	//入库：图片消息 content=唯一文件名；文件消息 content="唯一文件名|原始文件名"
	//（| 分隔——unique_name 是 uuid 不含 |；下游通知/历史加载统一拆分）
	auto content = unique_name;
	if (msg_type == int(ChatMsgType::FILE) && !original_name.empty()) {
		content = unique_name + "|" + original_name;
	}

	auto chat_msg = std::make_shared<ChatMessage>();
	chat_msg->chat_time = timestamp;
	chat_msg->sender_id = uid;
	chat_msg->recv_id = touid;
	chat_msg->unique_id = unique_id;
	chat_msg->thread_id = thread_id;
	chat_msg->content = content;
	chat_msg->status = 0;   //资源未上传完成
	chat_msg->msg_type = msg_type;

	std::vector<std::shared_ptr<ChatMessage>> chat_datas;
	chat_datas.push_back(chat_msg);
	//插入数据库（回填自增 message_id，客户端拿到它才发 1037 传文件）
	MysqlMgr::GetInstance()->AddChatMsg(chat_datas);

	rtvalue["message_id"] = chat_msg->message_id;
	rtvalue["status"] = chat_msg->status;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_IMG_CHAT_MSG_RSP);
		});
}

// 1055 建群：发起人选好友成群。
// 建群即生效（成员进 group_chat_member 表）——受邀请成员不实时收到通知，
// 下次登录 1025 拉会话列表时自然看到群聊（与老师教程的离线处理水平一致）
void LogicSystem::DealCreateGroup(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid = root["uid"].asInt();
	auto name = root["name"].asString();

	Json::Value rtvalue;
	rtvalue["error"] = ErrorCodes::Success;

	// 受邀成员列表
	std::vector<int> member_uids;
	for (const auto& v : root["member_uids"]) {
		member_uids.push_back(v.asInt());
	}
	// 群至少要有发起人之外的一个成员
	if (member_uids.empty()) {
		rtvalue["error"] = ErrorCodes::CREATE_CHAT_FAILED;
		return;
	}

	int thread_id = 0;
	bool res = MysqlMgr::GetInstance()->CreateGroup(uid, name, member_uids, thread_id);
	if (!res) {
		rtvalue["error"] = ErrorCodes::CREATE_CHAT_FAILED;
		return;
	}

	rtvalue["thread_id"] = thread_id;
	rtvalue["name"] = name;
	//回传全部成员（含发起人），客户端可显示"共 N 人"
	for (auto m_uid : member_uids) {
		rtvalue["member_uids"].append(m_uid);
	}
	rtvalue["member_uids"].append(uid);

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_CREATE_GROUP_RSP);
		});
}

