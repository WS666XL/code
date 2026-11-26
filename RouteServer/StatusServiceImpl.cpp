#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include"RedisMgr.h"
std::string generate_unique_string() {
	// 创建UUID对象
	boost::uuids::uuid uuid = boost::uuids::random_generator()();

	// 将UUID转换为字符串
	std::string unique_string = to_string(uuid);

	return unique_string;
}

Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
	std::string prefix("qingxun route server has received :  ");
	const auto& server = getChatServer();
	reply->set_host(server.host);
	reply->set_port(server.port);
	reply->set_error(ErrorCodes::Success);
	reply->set_token(generate_unique_string());
	insertToken(request->uid(), reply->token());
	return Status::OK;
}

StatusServiceImpl::StatusServiceImpl()
{
	auto& cfg = ConfigMgr::Inst();
	auto server_list = cfg["MsgNodes"]["Name"];

	std::vector<std::string> words;

	std::stringstream ss(server_list);
	std::string word;

	while (std::getline(ss, word, ',')) {
		words.push_back(word);
	}

	for (auto& word : words) {
		if (cfg[word]["Name"].empty()) {
			continue;
		}

		ChatServer server;
		server.port = cfg[word]["Port"];
		server.host = cfg[word]["Host"];
		server.name = cfg[word]["Name"];
		_servers[server.name] = server;
	}
}

ChatServer StatusServiceImpl::getChatServer() {
	// 锁内仅拷贝服务列表快照，锁外并发查询 Redis，避免多路 gRPC 登录请求在互斥锁上串行排队
	std::vector<ChatServer> candidates;
	{
		std::lock_guard<std::mutex> guard(_server_mtx);
		candidates.reserve(_servers.size());
		for (const auto& kv : _servers) {
			candidates.push_back(kv.second);
		}
	}

	ChatServer minServer = candidates.front();
	minServer.con_count = INT_MAX;
	for (auto& server : candidates) {
		auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.name);
		if (count_str.empty()) {
			server.con_count = INT_MAX;
		}
		else {
			server.con_count = std::stoi(count_str);
		}
		if (server.con_count < minServer.con_count) {
			minServer = server;
		}
	}

	return minServer;
}

Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
	auto uid = request->uid();
	auto token = request->token();

	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;
	std::string token_value = "";
	bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
	if (!success) {
		reply->set_error(ErrorCodes::UidInvalid);
		return Status::OK;
	}

	if (token_value != token) {
		reply->set_error(ErrorCodes::TokenInvalid);
		return Status::OK;
	}
	reply->set_error(ErrorCodes::Success);
	reply->set_uid(uid);
	reply->set_token(token);
	return Status::OK;
}

void StatusServiceImpl::insertToken(int uid, std::string token)
{
	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;
	RedisMgr::GetInstance()->Set(token_key, token);
}

