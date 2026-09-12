// ============================================================================
// GateServer.cpp —— 程序入口（main 函数所在）
//
// 整个程序其实就一根「事件循环」在驱动：
//   main 把监听任务丢进 io_context，然后 ioc.run() 开始死循环地
//   「取任务 → 执行回调 → 再取任务」，所有收发数据都发生在这个循环里。
// ============================================================================

#include"const.h"
#include"CServer.h"

int main()
{
    try
    {
        // ① 要监听的端口号：8080（浏览器访问 http://127.0.0.1:8080 就能连上）
        unsigned short port = static_cast<unsigned short>(8080);

        // ② 创建 io_context（事件循环 / 任务队列）。
        //    花括号里的 1 是「并发提示」，表示只用 1 个线程来跑。
        //    ★ 重要：必须调用 ioc.run()，注册在上面的异步任务才会被执行；
        //      不 run 的话，所有 async_xxx 都不会真正跑起来。
        net::io_context ioc{ 1 };

        // ③ 注册信号监听，实现「优雅退出」
        //    SIGINT  = 在终端按 Ctrl+C
        //    SIGTERM = 系统/运维工具发出的结束进程信号（kill 默认就发这个）
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

        //    异步等待信号：收到信号后调用这个 lambda
        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {

            if (error) {
                return;
            }
            // 停止事件循环 → ioc.run() 会返回 → main 结束 → 进程正常退出。
            // 好处：不用强杀进程，还能在退出前做清理工作。
            ioc.stop();
            });

        // ④ 创建服务器对象并启动监听。
        //    用 make_shared 创建（因为 CServer 继承了 enable_shared_from_this，
        //    必须被 shared_ptr 管理，内部才能调用 shared_from_this）。
        //    这里没有保存返回的 shared_ptr，但因为 CServer::Start 的回调里
        //    捕获了 self（指向自己的 shared_ptr），所以对象不会被释放。
        std::make_shared<CServer>(ioc, port)->Start();

        // ⑤ 启动事件循环（阻塞在这一行），程序开始真正工作：
        //    不断检查有没有到期的异步任务，有就执行它的回调。
        ioc.run();
    }
    catch (std::exception const& e)
    {
        // 任何异常兜底：打印错误原因，返回非 0 表示程序异常结束
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

// ---------- 工具函数：把一个字符转成 16 进制表示 ----------
// 例如：0 → 字符 '0'(ASCII 48)，10 → 'A'(ASCII 65)
// 用途：URL 编码（Percent-Encoding）。后面做验证码、邮件验证等功能时，
//       需要把特殊字符转成 %XX 的形式拼进 URL，就会用到它。
// 当前还没有被调用，先留着备用。
//char 转为16进制
unsigned char ToHex(unsigned char x)
{
    return  x > 9 ? x + 55 : x + 48;
}