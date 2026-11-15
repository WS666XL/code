#pragma once
#include "Singleton.h"
#include "const.h"
#include <memory>
#include <map>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "message.pb.h"

//proto 全部在 message 命名空间里，打开后代码里直接写 ChatService/NotifyChatImgRsp
using namespace message;

//gRPC 连接池：到一台 ChatServer 的 stub 池（图片通知调用频率低，5 条足够）
class ChatServerConPool {
public:
	ChatServerConPool(const std::string& host, const std::string& port)
		: host_(host), port_(port), b_stop_(false) {
		auto channel = grpc::CreateChannel(host + ":" + port,
			grpc::InsecureChannelCredentials());
		stub_ = ChatService::NewStub(channel);
	}

	~ChatServerConPool() {
		std::lock_guard<std::mutex> lock(mutex_);
		b_stop_ = true;
		cond_.notify_all();
	}

	std::unique_ptr<ChatService::Stub> getConnection() {
		std::unique_lock<std::mutex> lock(mutex_);
		cond_.wait(lock, [this] {
			if (b_stop_) {
				return true;
			}
			return !connections_.empty();
			});
		if (b_stop_) {
			return nullptr;
		}
		auto context = std::move(connections_.front());
		connections_.pop();
		return context;
	}

	void returnConnection(std::unique_ptr<ChatService::Stub> context) {
		std::lock_guard<std::mutex> lock(mutex_);
		connections_.push(std::move(context));
		cond_.notify_one();
	}

private:
	std::atomic<bool> b_stop_;
	std::string host_;
	std::string port_;
	std::queue<std::unique_ptr<ChatService::Stub>> connections_;
	std::mutex mutex_;
	std::condition_variable cond_;
	std::unique_ptr<ChatService::Stub> stub_{ nullptr };
};

//ChatServer gRPC 客户端单例：资源服务器把聊天图片落盘完成后，
//按收图人所在节点（Redis uip_<uid> 查到）调用对应 ChatServer 的 NotifyChatImgMsg，
//由那台节点把 1039 图片消息通知推给在线的收图人
class ChatServerGrpcClient : public Singleton<ChatServerGrpcClient>
{
	friend class Singleton<ChatServerGrpcClient>;
public:
	~ChatServerGrpcClient() {
	}
	NotifyChatImgRsp NotifyChatImgMsg(int message_id, std::string chatserver);
private:
	ChatServerGrpcClient();
	std::map<std::string, std::unique_ptr<ChatServerConPool>> _pools;
};
