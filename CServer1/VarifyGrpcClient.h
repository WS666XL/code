// VarifyGrpcClient.h —— gRPC 客户端封装
// 角色：GateServer 在这里当「客户端」，通过 gRPC 调用远端的 VarifyServer（验证码服务）。
// 调用链：HTTP 请求进来 → LogicSystem 的处理函数 → VerifyGrpcClient::GetVarifyCode() → VarifyServer
//
// gRPC 客户端三件套（本文件的核心就是这三样）：
//   Channel（通道）：到服务端的一条「逻辑连接」，内部封装 HTTP/2 连接、重连、多路复用。
//                    注意它是懒连接——CreateChannel 不会立刻握手，第一次真正发 RPC 时才去连。
//   Stub（存根）  ：由 protoc 根据 message.proto 生成的代理对象，方法名与 proto 里一一对应。
//                    调 stub_->GetVarifyCode(...) 就像调本地函数，序列化/发包/收包都由它包办。
//   ClientContext ：单次调用的「上下文」，一次 RPC 配一个，不能跨调用复用。
#pragma once
#include <grpcpp/grpcpp.h>          // gRPC C++ 总头文件（Channel / Stub / Status / ClientContext 都在这）
#include "message.grpc.pb.h"        // protoc 由 message.proto 生成的桩代码，VarifyService::Stub 在这里
#include "const.h"                  // 用到 ErrorCodes::RPCFailed
#include "Singleton.h" 
#include"ConfigMgr.h"
// 单例模板：全局只保留一个客户端、一条 Channel

using grpc::Channel;                // 通信通道：只管"连到哪、怎么连"，不管"调什么方法"
using grpc::Status;                 // 一次 RPC 的传输层结果：ok() / error_code() / error_message()
                                    // 注意与业务错误区分——业务错误在 Rsp 的 error 字段里
using grpc::ClientContext;          // 单次请求的上下文：可设置超时、metadata、取消、鉴权等

// 下面三个类型由 message.proto 里的 package message 生成，这里用 using 缩短写法
using message::GetVarifyReq;        // 请求体：{ string email = 1; }
using message::GetVarifyRsp;        // 响应体：{ int32 error = 1; string email = 2; string code = 3; }
using message::VarifyService;       // 服务：对应 proto 里的 service VarifyService

// 单例：整个进程共用一个 Channel 和 Stub 就够了（gRPC 的 Stub 是线程安全的，可并发调用）


class RPConPool {
public:
    RPConPool(size_t poolSize, std::string host, std::string port)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        for (size_t i = 0; i < poolSize_; ++i) {

            std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
                grpc::InsecureChannelCredentials());
            //VarifyService::NewStub(channel) 是一次函数调用的返回值,是一个临时对象,不能直接 push 到队列中,所以要用 std::move 转移所有权   
            //connections_.push(VarifyService::NewStub(channel));//有&&版本

            auto m = VarifyService::NewStub(channel);
            connections_.push(std::move(m));
        }
    }

    ~RPConPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        Close();
        while (!connections_.empty()) {
            connections_.pop();
        }
    }

    std::unique_ptr<VarifyService::Stub> getConnection() {
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
        auto context = std::move(connections_.front());
        connections_.pop();
        return context;
    }

    void returnConnection(std::unique_ptr<VarifyService::Stub> context) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;
        }
        connections_.push(std::move(context));
        cond_.notify_one();
    }

    void Close() {
        b_stop_ = true;
        cond_.notify_all();
    }

private:
    std::atomic<bool> b_stop_;
    size_t poolSize_;
    std::string host_;
    std::string port_;
    std::queue<std::unique_ptr<VarifyService::Stub>> connections_;
    std::mutex mutex_;
    std::condition_variable cond_;
};


class VerifyGrpcClient :public Singleton<VerifyGrpcClient>
{
    friend class Singleton<VerifyGrpcClient>;   // 构造函数为 private，放行单例基类去 new 本类
public:

    // ── 发起一次「同步（阻塞）RPC」：请求验证码 ──────────────────────────────
    // 同步调用的固定四步：① 建上下文 → ② 填请求 → ③ 收响应 → ④ 检查状态
    GetVarifyRsp GetVarifyCode(std::string email) {
        ClientContext context;               // ① 本次调用的上下文，每次调用都要新建
        GetVarifyRsp reply;                  // ③ 出参：由 gRPC 反序列化后填入，调用前是空对象
        GetVarifyReq request;                // ② 入参：proto 生成的 setter，对应 email 字段
        request.set_email(email);

        // 一行完成 RPC：把 context / request 发出去，结果写回 reply
        // 参数顺序固定为 (上下文, 请求, 响应)，由 message.proto 生成，不能自己改
        // 这是同步调用——函数会一直阻塞到服务端返回（或失败）为止
        auto stub = pool_->getConnection();

        Status status = stub->GetVarifyCode(&context, request, &reply);

        if (status.ok()) {
            // 传输层成功。业务是否成功要看 reply.error，这里只负责把响应交回上层
            pool_->returnConnection(std::move(stub));
            return reply;
        }
        else {
            // 传输层失败：连不上、服务端没起、超时等
            // 此时 reply 里没有任何有效内容，手工塞一个业务错误码方便上层识别
            pool_->returnConnection(std::move(stub));
            reply.set_error(ErrorCodes::RPCFailed);   // 1002
            return reply;
        }
    }

private:
    
    VerifyGrpcClient();
    std::unique_ptr<RPConPool> pool_;

};
