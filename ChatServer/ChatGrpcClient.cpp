#include "ChatGrpcClient.h"
#include "Logger.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "UserMgr.h"

#include "CSession.h"
#include "MysqlMgr.h"
ChatGrpcClient::ChatGrpcClient()
{
	auto& config = ConfigMgr::Inst();
	//读 [PeerServer] 段的 Servers 键（逗号分隔的节点名列表），为每个对端节点建 gRPC 连接池
	auto server_list = config["PeerServer"]["Servers"];
	std::vector<std::string>words;
	std::stringstream ss(server_list);
	std::string word;
	while (std::getline(ss, word, ',')) {
		words.push_back(word);
	}
	for(auto& word : words) {
		if(word.empty()) {
			continue;
		}
		_pools[config[word]["Name"]] = std::make_unique<ChatConPool>(16,config[word]["Host"],config[word]["Port"]);
	}

}
AddFriendRsp ChatGrpcClient::NotifyAddFriend(std::string server_ip, const AddFriendReq& req)
{
	AddFriendRsp rsp;
	rsp.set_error(ErrorCodes::Success);
	Defer defer([&rsp, &req]() {
		rsp.set_applyuid(req.applyuid());
		rsp.set_touid(req.touid());
		});

	auto find_iter = _pools.find(server_ip);
	if (find_iter == _pools.end()) {
		return rsp;
	}

	auto& pool = find_iter->second;
	ClientContext context;
	auto stub = pool->getConnection();
	if (!stub) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}
	Defer defercon([&stub, this, &pool]() {
		pool->returnConnection(std::move(stub));
		});
	Status status = stub->NotifyAddFriend(&context, req, &rsp);

	if (!status.ok()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	return rsp;
}


bool ChatGrpcClient::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
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
		LLOG_DEBUG << "user login uid is " << userinfo->uid << " name is "
			<< userinfo->name << " email is " << userinfo->email << endl;
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

AuthFriendRsp ChatGrpcClient::NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req) {
	AuthFriendRsp rsp;
	rsp.set_error(ErrorCodes::Success);

	Defer defer([&rsp, &req]() {
		rsp.set_fromuid(req.fromuid());
		rsp.set_touid(req.touid());
		});

	auto find_iter = _pools.find(server_ip);
	if (find_iter == _pools.end()) {
		return rsp;
	}

	auto& pool = find_iter->second;
	ClientContext context;
	auto stub = pool->getConnection();
	if (!stub) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}
	Defer defercon([&stub, this, &pool]() {
		pool->returnConnection(std::move(stub));
		});
	Status status = stub->NotifyAuthFriend(&context, req, &rsp);

	if (!status.ok()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	return rsp;
}

TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(std::string server_ip,
	const TextChatMsgReq& req, const Json::Value& rtvalue) {

	TextChatMsgRsp rsp;
	rsp.set_error(ErrorCodes::Success);

	Defer defer([&rsp, &req]() {
		rsp.set_fromuid(req.fromuid());
		rsp.set_touid(req.touid());
		for (const auto& text_data : req.textmsgs()) {
			TextChatData* new_msg = rsp.add_textmsgs();
			new_msg->set_unique_id(text_data.unique_id());
			new_msg->set_msgcontent(text_data.msgcontent());
		}

		});

	auto find_iter = _pools.find(server_ip);
	if (find_iter == _pools.end()) {
		return rsp;
	}

	auto& pool = find_iter->second;
	ClientContext context;
	auto stub = pool->getConnection();
	if (!stub) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}
	Defer defercon([&stub, this, &pool]() {
		pool->returnConnection(std::move(stub));
		});
	Status status = stub->NotifyTextChatMsg(&context, req, &rsp);

	if (!status.ok()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	return rsp;
}

// 跨服踢人：uid 上次登录在 server_ip 这台节点上，通知它把旧连接踢掉。
// 先填好默认 Success，即使目标节点不在连接池里（比如那台节点挂了）也能正常返回，
// 但真正 RPC 失败时不会在 Defer 里把 RPCFailed 覆盖掉
KickUserRsp ChatGrpcClient::NotifyKickUser(std::string server_ip, const KickUserReq& req)
{
	KickUserRsp rsp;
	rsp.set_error(ErrorCodes::Success);
	Defer defer([&rsp, &req]() {
		rsp.set_uid(req.uid());
		});

	auto find_iter = _pools.find(server_ip);
	if (find_iter == _pools.end()) {
		LLOG_INFO << "NotifyKickUser: no grpc pool for server [" << server_ip << "]" << std::endl;
		return rsp;
	}

	auto& pool = find_iter->second;
	ClientContext context;
	auto stub = pool->getConnection();
	if (!stub) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}
	Defer defercon([&stub, this, &pool]() {
		pool->returnConnection(std::move(stub));
		});
	Status status = stub->NotifyKickUser(&context, req, &rsp);
	LLOG_INFO << "NotifyKickUser rpc to [" << server_ip << "] uid=" << req.uid()
		<< " status=" << (status.ok() ? "ok" : status.error_message()) << std::endl;

	if (!status.ok()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	return rsp;
}