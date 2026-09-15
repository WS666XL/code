// GateServer.cpp —— 程序入口（main 所在）：事件循环驱动
#include"const.h"
#include"CServer.h"
#include"ConfigMgr.h"
int main()
{
    ConfigMgr gCfgMgr;   // 构造即解析 config.ini（细节见 ConfigMgr.cpp）；它是 main 的局部变量，
                         // 与 const.h 里 extern 声明的全局 gCfgMgr 同名，此处局部变量把全局的遮住了
    std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
    unsigned short gate_port = atoi(gate_port_str.c_str());   // 从 config.ini 读出的端口
    try
    {
        // ① 监听端口：访问 http://127.0.0.1:8080 即可连上
        // 【注意】这里没用上面的 gate_port，端口是写死的 8080。
        // 想让 config.ini 生效，把这行改成 unsigned short port = gate_port; 即可（此处只加注释，未改代码）
        unsigned short port = static_cast<unsigned short>(8080);

        // ② 创建事件循环（1 表示只用 1 个线程）；必须 ioc.run() 异步任务才会执行
        net::io_context ioc{ 1 };

        // ③ 信号监听，实现优雅退出：SIGINT = Ctrl+C，SIGTERM = kill
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {

            if (error) {
                return;
            }
            ioc.stop();   // 停止事件循环 → run() 返回 → 进程正常退出
            });

        // ④ 创建服务器并启动监听（CServer 需被 shared_ptr 管理才能用 shared_from_this）
        std::make_shared<CServer>(ioc, port)->Start();
		std::cout << "Server started on port " << port << std::endl;
        // ⑤ 启动事件循环（阻塞在此），不断执行到期的异步任务回调
        ioc.run();
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;   // 异常兜底，返回非 0
        return EXIT_FAILURE;
    }
}


