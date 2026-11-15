#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <mutex>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginReq;
using message::LoginRsp;
using message::StatusService;

// 聊天服务器的信息
class  ChatServer {
public:
    ChatServer() :host(""), port(""), name(""), con_count(0) {}
    ChatServer(const ChatServer& cs) :host(cs.host), port(cs.port), name(cs.name), con_count(cs.con_count) {}
    ChatServer& operator=(const ChatServer& cs) {
        if (&cs == this) {
            return *this;
        }

        host = cs.host;
        name = cs.name;
        port = cs.port;
        con_count = cs.con_count;
        return *this;
    }
    std::string host;
    std::string port;
    std::string name;
    int con_count;
};
// 状态服务器的 gRPC 服务实现类
class StatusServiceImpl final : public StatusService::Service
{
public:
    StatusServiceImpl();

    // 处理获取聊天服务器的请求，返回服务器地址等信息
    Status GetChatServer(ServerContext* context, const GetChatServerReq* request,
        GetChatServerRsp* reply) override;

    // 处理用户登录请求，验证登录状态并返回结果
    Status Login(ServerContext* context, const LoginReq* request,
        LoginRsp* reply) override;

private:
    // 保存用户 ID 和对应的 token
    void insertToken(int uid, std::string token);

    // 获取一个合适的聊天服务器
    ChatServer getChatServer();

    // 保存聊天服务器信息，key 为服务器标识
    std::unordered_map<std::string, ChatServer> _servers;
    std::mutex _server_mtx;  // 保护聊天服务器信息的互斥锁

    // 保存用户 ID 和对应的 token，key 为用户 ID
   // std::unordered_map<int, std::string> _tokens;
    //std::mutex _token_mtx;  // 保护 token 信息的互斥锁
};
