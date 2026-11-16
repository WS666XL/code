#include "RedisMgr.h"
#include "ConfigMgr.h"
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>

//单例构造：从 config.ini 的 [Redis] 段读连接参数建池
//资源服务器与 ChatServer 连同一个 Redis（127.0.0.1:6380），
//所以 ChatServer 写的 utoken_<uid> / ubaseinfo_<uid> 这里直接能读
RedisMgr::RedisMgr() {
	auto& gCfgMgr = ConfigMgr::Inst();
	auto host = gCfgMgr["Redis"]["Host"];
	auto port = gCfgMgr["Redis"]["Port"];
	auto pwd = gCfgMgr["Redis"]["Passwd"];
	_con_pool.reset(new RedisConPool(5, host.c_str(), atoi(port.c_str()), pwd.c_str()));
}

RedisMgr::~RedisMgr() {

}

bool RedisMgr::Get(const std::string& key, std::string& value)
{
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "GET %s", key.c_str());
	if (reply == NULL) {
		std::cout << "[ GET  " << key << " ] failed" << std::endl;
		_con_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
		std::cout << "[ GET  " << key << " ] failed" << std::endl;
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	freeReplyObject(reply);

	std::cout << "Succeed to execute command [ GET " << key << "  ]" << std::endl;
	_con_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::SetExp(const std::string& key, const std::string& value, int expire_seconds) {
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "SETEX %s %d %s",
		key.c_str(), expire_seconds, value.c_str());
	if (reply == nullptr) {
		std::cout << "Execut command [ SETEX " << key << " ] failure ! " << std::endl;
		_con_pool->returnConnection(connect);
		return false;
	}
	if (!(reply->type == REDIS_REPLY_STATUS &&
		(strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0))) {
		std::cout << "Execut command [ SETEX " << key << " ] failure ! " << std::endl;
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::Del(const std::string& key) {
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "DEL %s", key.c_str());
	if (reply == nullptr) {
		std::cout << "Execut command [ DEL " << key << " ] failure ! " << std::endl;
		_con_pool->returnConnection(connect);
		return false;
	}
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::SetFileInfo(const std::string& key, std::shared_ptr<FileInfo> file_info) {
	Json::Value root;
	root["file_path_str"] = file_info->_file_path_str;
	root["name"] = file_info->_name;
	root["seq"] = file_info->_seq;
	//int64 用字符串存：Json 数字在部分实现里会走 double，超过 2^53 会丢精度
	root["total_size"] = std::to_string(file_info->_total_size);
	root["trans_size"] = std::to_string(file_info->_trans_size);
	return SetExp("file_upload_" + key, root.toStyledString(), 3600);
}

std::shared_ptr<FileInfo> RedisMgr::GetFileInfo(const std::string& key) {
	std::string value;
	if (!Get("file_upload_" + key, value) || value.empty()) {
		return nullptr;
	}
	Json::Reader reader;
	Json::Value root;
	if (!reader.parse(value, root)) {
		return nullptr;
	}
	auto info = std::make_shared<FileInfo>();
	try {
		info->_file_path_str = root["file_path_str"].asString();
		info->_name = root["name"].asString();
		info->_seq = root["seq"].asInt();
		info->_total_size = std::stoll(root["total_size"].asString());
		info->_trans_size = std::stoll(root["trans_size"].asString());
	}
	catch (const std::exception& e) {
		std::cout << "parse upload file info failed: " << e.what() << std::endl;
		return nullptr;
	}
	return info;
}

bool RedisMgr::SetDownloadInfo(const std::string& key, std::shared_ptr<FileInfo> file_info) {
	Json::Value root;
	root["file_path_str"] = file_info->_file_path_str;
	root["name"] = file_info->_name;
	root["seq"] = file_info->_seq;
	root["total_size"] = std::to_string(file_info->_total_size);
	root["trans_size"] = std::to_string(file_info->_trans_size);
	return SetExp("file_download_" + key, root.toStyledString(), 3600);
}

std::shared_ptr<FileInfo> RedisMgr::GetDownloadInfo(const std::string& key) {
	std::string value;
	if (!Get("file_download_" + key, value) || value.empty()) {
		return nullptr;
	}
	Json::Reader reader;
	Json::Value root;
	if (!reader.parse(value, root)) {
		return nullptr;
	}
	auto info = std::make_shared<FileInfo>();
	try {
		info->_file_path_str = root["file_path_str"].asString();
		info->_name = root["name"].asString();
		info->_seq = root["seq"].asInt();
		info->_total_size = std::stoll(root["total_size"].asString());
		info->_trans_size = std::stoll(root["trans_size"].asString());
	}
	catch (const std::exception& e) {
		std::cout << "parse download file info failed: " << e.what() << std::endl;
		return nullptr;
	}
	return info;
}

bool RedisMgr::DelDownloadInfo(const std::string& key) {
	return Del("file_download_" + key);
}

bool RedisMgr::Set(const std::string& key, const std::string& value) {
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "SET %s %s", key.c_str(), value.c_str());

	//如果返回NULL则说明执行失败
	if (NULL == reply)
	{
		std::cout << "Execut command [ SET " << key << "  " << value << " ] failure ! " << std::endl;
		_con_pool->returnConnection(connect);
		return false;
	}

	//如果执行失败则释放连接
	if (!(reply->type == REDIS_REPLY_STATUS && (strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0)))
	{
		std::cout << "Execut command [ SET " << key << "  " << value << " ] failure ! " << std::endl;
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	freeReplyObject(reply);
	std::cout << "Execut command [ SET " << key << "  " << value << " ] success ! " << std::endl;
	_con_pool->returnConnection(connect);
	return true;
}
