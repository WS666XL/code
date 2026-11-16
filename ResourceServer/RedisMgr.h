#pragma once
#include "const.h"
#include "data.h"
#include "hiredis.h"
#include <queue>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include "Singleton.h"

//Redis 连接池：预建 5 条连接 + 后台心跳线程防断连。
//从 ChatServer 拷贝精简而来：资源服务器只用到 GET/SET 两种命令
//（GET 校验登录 token，SET 写回用户信息缓存），其余命令不再保留
class RedisConPool {
public:
	RedisConPool(size_t poolSize, const char* host, int port, const char* pwd)
		: poolSize_(poolSize), host_(host), port_(port), b_stop_(false), pwd_(pwd), counter_(0) {
		for (size_t i = 0; i < poolSize_; ++i) {
			auto* context = redisConnect(host, port);
			if (context == nullptr || context->err != 0) {
				if (context != nullptr) {
					redisFree(context);
				}
				continue;
			}

			auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd);
			if (reply->type == REDIS_REPLY_ERROR) {
				std::cout << "认证失败" << std::endl;
				freeReplyObject(reply);
				continue;
			}

			freeReplyObject(reply);
			std::cout << "认证成功" << std::endl;
			connections_.push(context);
		}

		//心跳线程：每 60 秒 PING 一轮所有连接，断掉的重建
		check_thread_ = std::thread([this]() {
			while (!b_stop_) {
				counter_++;
				if (counter_ >= 60) {
					checkThread();
					counter_ = 0;
				}

				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
			});

	}

	~RedisConPool() {

	}

	void ClearConnections() {
		std::lock_guard<std::mutex> lock(mutex_);
		while (!connections_.empty()) {
			auto* context = connections_.front();
			redisFree(context);
			connections_.pop();
		}
	}

	redisContext* getConnection() {
		std::unique_lock<std::mutex> lock(mutex_);
		cond_.wait(lock, [this] {
			if (b_stop_) {
				return true;
			}
			return !connections_.empty();
			});
		//如果停止则直接返回空指针
		if (b_stop_) {
			return  nullptr;
		}
		auto* context = connections_.front();
		connections_.pop();
		return context;
	}

	void returnConnection(redisContext* context) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (b_stop_) {
			return;
		}
		connections_.push(context);
		cond_.notify_one();
	}

	void Close() {
		b_stop_ = true;
		cond_.notify_all();
		check_thread_.join();
	}

private:
	void checkThread() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (b_stop_) {
			return;
		}
		auto pool_size = connections_.size();
		for (int i = 0; i < pool_size && !b_stop_; i++) {
			auto* context = connections_.front();
			connections_.pop();
			try {
				auto reply = (redisReply*)redisCommand(context, "PING");
				if (!reply) {
					std::cout << "reply is null, redis ping failed: " << std::endl;
					connections_.push(context);
					continue;
				}
				freeReplyObject(reply);
				connections_.push(context);
			}
			catch (std::exception& exp) {
				std::cout << "Error keeping connection alive: " << exp.what() << std::endl;
				redisFree(context);
				context = redisConnect(host_, port_);
				if (context == nullptr || context->err != 0) {
					if (context != nullptr) {
						redisFree(context);
					}
					continue;
				}

				auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd_);
				if (reply->type == REDIS_REPLY_ERROR) {
					std::cout << "认证失败" << std::endl;
					freeReplyObject(reply);
					continue;
				}

				freeReplyObject(reply);
				std::cout << "认证成功" << std::endl;
				connections_.push(context);
			}
		}
	}
	std::atomic<bool> b_stop_;
	size_t poolSize_;
	const char* host_;
	const char* pwd_;
	int port_;
	std::queue<redisContext*> connections_;
	std::mutex mutex_;
	std::condition_variable cond_;
	std::thread  check_thread_;
	int counter_;
};

class RedisMgr : public Singleton<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	~RedisMgr();
	bool Get(const std::string& key, std::string& value);
	bool Set(const std::string& key, const std::string& value);
	//带过期时间的 SET（SETEX）：断点续传的进度只保留 1 小时，
	//过期后客户端再来续传会拿到 FileNotExists，自动从第 1 块重传
	bool SetExp(const std::string& key, const std::string& value, int expire_seconds);
	bool Del(const std::string& key);
	//上传进度：key 由调用方拼（uid|md5），这里只负责序列化/反序列化 FileInfo
	bool SetFileInfo(const std::string& key, std::shared_ptr<FileInfo> file_info);
	std::shared_ptr<FileInfo> GetFileInfo(const std::string& key);
	//下载进度：单独一套前缀，避免和上传进度串号
	bool SetDownloadInfo(const std::string& key, std::shared_ptr<FileInfo> file_info);
	std::shared_ptr<FileInfo> GetDownloadInfo(const std::string& key);
	bool DelDownloadInfo(const std::string& key);
	void Close() {
		_con_pool->Close();
		_con_pool->ClearConnections();
	}
private:
	RedisMgr();
	std::unique_ptr<RedisConPool>  _con_pool;
};
