// ChatServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ChatServiceImpl.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "LogicSystem.h"
#include "RedisMgr.h"
#include <csignal>
#include <mutex>
#include <thread>
using namespace std;
bool bstop = false;
std::condition_variable cond_quit;
std::mutex mutex_quit;

int main() {
  auto &cfg = ConfigMgr::Inst();
  auto server_name = cfg["SelfServer"]["Name"];
  // 日志：控制台 + logs/<服务名>.log，级别/目录可在 config.ini 的 [Log] 里改
  xulei::Logger::Init(server_name, cfg["Log"]["Path"], cfg["Log"]["Level"]);
  try {
    auto pool = AsioIOServicePool::GetInstance();
    // 将登录数设置为0
    RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, "0");

    // 定义一个GrpcServer

    std::string server_address(cfg["SelfServer"]["Host"] + ":" +
                               cfg["SelfServer"]["RPCPort"]); // 监听端口
    ChatServiceImpl service;
    grpc::ServerBuilder builder;
    // 监听端口和添加服务
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    // 构建并启动gRPC服务器
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    LLOG_INFO << "RPC Server listening on " << server_address << std::endl;

    // 单独启动一个线程处理grpc服务
    std::thread grpc_server_thread([&server]() { server->Wait(); });
    // 线程创建后到 join 之间若抛异常，joinable 的 std::thread 析构会直接 terminate，
    // 日志只剩 "terminate called without an active exception"。
    // 这里先收尾再往外抛，保证能看到真正的错误（比如端口绑定失败）
    try {
      boost::asio::io_context io_context;
      boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
      signals.async_wait([&io_context, pool, &server](auto, auto) {
        io_context.stop();
        pool->Stop();
        server->Shutdown();
      });
      auto port_str = cfg["SelfServer"]["Port"];
      // CServer 用 shared_ptr 管理：LoginHandler/NotifyKickUser 踢人时要用它清除
      // session
      auto pointer_server =
          std::make_shared<CServer>(io_context, atoi(port_str.c_str()));
      // 把 CServer 注册给 gRPC 服务（跨服踢人用）
      service.RegisterServer(pointer_server);
      // 把 CServer 注册给逻辑类（本服踢人用）
      LogicSystem::GetInstance()->SetServer(pointer_server);
      io_context.run();

      RedisMgr::GetInstance()->HDel(LOGIN_COUNT, server_name);
      RedisMgr::GetInstance()->Close();
    } catch (...) {
      server->Shutdown();
      if (grpc_server_thread.joinable()) {
        grpc_server_thread.join();
      }
      throw;
    }
    grpc_server_thread.join();
  } catch (std::exception &e) {
    LLOG_ERROR << "Exception: " << e.what() << endl;
  }
}
