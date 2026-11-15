#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "message.pb.h"
#include <mutex>
#include "data.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::AddFriendReq;
using message::AddFriendRsp;

using message::AuthFriendReq;
using message::AuthFriendRsp;

using message::ChatService;
using message::TextChatMsgReq;
using message::TextChatMsgRsp;
using message::TextChatData;

using message::KickUserReq;
using message::KickUserRsp;
using message::NotifyChatImgReq;
using message::NotifyChatImgRsp;

class CServer; // 前置声明：RegisterServer 参数用到

class ChatServiceImpl final : public ChatService::Service
{
public:
	ChatServiceImpl();
	Status NotifyAddFriend(ServerContext* context, const AddFriendReq* request,
		AddFriendRsp* reply) override;

	Status NotifyAuthFriend(ServerContext* context,
		const AuthFriendReq* request, AuthFriendRsp* response) override;

	Status NotifyTextChatMsg(::grpc::ServerContext* context,
		const TextChatMsgReq* request, TextChatMsgRsp* response) override;

	// 资源服务器图片落盘完成后调这里：转发 1039 图片消息通知给本节点的收图人
	Status NotifyChatImgMsg(::grpc::ServerContext* context,
		const NotifyChatImgReq* request, NotifyChatImgRsp* response) override;

	// 跨服踢人：另一台 ChatServer 发现本节点持有 uid 的旧连接时，通过 gRPC 调这里
	Status NotifyKickUser(::grpc::ServerContext* context,
		const KickUserReq* request, KickUserRsp* response) override;

	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);

	// main 里把 CServer 智能指针注入进来，踢人时用它清除 session
	void RegisterServer(std::shared_ptr<CServer> pServer);

private:
	std::shared_ptr<CServer> _p_server;
};