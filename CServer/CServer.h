// ============================================================================
// CServer.h —— 服务器「监听」类（相当于餐厅门口的迎宾员）
//
// 职责很单纯，只干一件事：在指定端口上盯着，谁（客户端）来了就把他招呼进来，
// 然后交给 HttpConnection 去具体服务，自己马上回去继续盯门口。
//
// 它不解析 HTTP、不处理业务，也不保存数据。
// ============================================================================

#pragma once
#include"const.h"
#include"HttpConnection.h"
// 为什么继承 std::enable_shared_from_this<CServer> ？
//   因为 Start() 里投递的是「异步」操作：async_accept 一调用就立刻返回，
//   真正干活的是后面某个时刻才会执行的 lambda 回调。
//   等回调执行时，如果 CServer 对象已经被销毁，里面的 socket 也就没了 —— 会崩。
//   继承这个父类后，就能在成员函数里调用 shared_from_this()，
//   拿到一个「指向自己的 shared_ptr」，把它捕获进 lambda，
//   这样在回调执行之前，对象一定还活着（引用计数至少为 1）。
//   这个技巧在 Asio 异步编程里非常常见，记住它。
class CServer :public std::enable_shared_from_this<CServer>//enable_shared_from_this父类，父类有个智能指针指向CServer
{
public:
    // 构造函数
    //   ioc   ：事件循环核心（io_context），所有异步任务都注册到它上面、由它调度执行
    //   port  ：要监听的端口号（比如 8080）
    //   注意参数都用「引用」，表示不拷贝、直接沿用外面传进来的那个对象
    CServer(boost::asio::io_context& ioc, unsigned short& port);

    // 开始异步地接受客户端连接。
    // 调用一次就挂一个「等待连接」的异步任务，成功后处理完会再次调用自己，形成循环监听。
    // ---------- 开始异步接受连接 ----------
    // 整体思路：挂一个异步 accept 任务 → 有客户端连进来就交给 HttpConnection 处理
    //          → 不管成功失败，最后都再调一次 Start()，继续等下一个客户端（循环监听）
    void Start()
    {
        // ① 拿到指向自己的 shared_ptr，后面捕获进 lambda，
        //    防止 Start() 函数返回后 CServer 对象被析构、socket 被释放
        auto self = shared_from_this();

        // ② 发起异步 accept：函数立刻返回，不阻塞；
        //    等真的有客户端连进来时，ioc 会挑个线程执行下面这个 lambda 回调
        //    参数 ec 是错误码：没出错时为空，出错时里面是失败原因
        _acceptor.async_accept(_socket, [self](beast::error_code ec) {//防止start函数返回后，CServer对象被析构，导致socket被释放，
            //所以这里使用shared_from_this()获取一个shared_ptr<CServer>，保证CServer对象在异步操作完成前不会被释放
            try {
                //出错则放弃这个连接，继续监听新链接
                if (ec) {
                    self->Start();   // 出错就丢弃这次连接，重新挂一个监听，不能让服务器停摆
                    return;
                }

                // ③ 有客户端连进来了：
                //    用 std::move 把 _socket「移交」给新建的 HttpConnection（移动后 CServer 里这个就空了），
                //    由 HttpConnection 负责后续的读请求、写响应；然后调用它的 Start() 开始工作。
                //    这里用 make_shared 创建，配合继承的 enable_shared_from_this，
                //    保证 HttpConnection 在它的异步回调完成前不会被销毁。
                //
                //    ★ 类比：一个 HttpConnection 就代表「一个客户端的一次会话」，
                //      多个客户端同时连，就会有多个 HttpConnection 对象并存。
                //处理新链接，创建HpptConnection类管理新连接
                std::make_shared<HttpConnection>(std::move(self->_socket))->Start();

                //继续监听（④ 回到 ①，继续等下一个客户端）
                self->Start();
            }
            catch (std::exception& exp) {
                // 处理过程中抛出任何异常都兜住，打印出来，然后继续监听，避免整个服务器挂掉
                std::cout << "exception is " << exp.what() << std::endl;
                self->Start();
            }
            });
    }
private:
    // 接收器（监听器）。传统 socket 编程要分三步：socket() 创建 → bind() 绑端口 → listen() 监听，
    // 而 asio 里构造一个 acceptor 对象就一次性搞定这三件事。
    tcp::acceptor  _acceptor;// 这一行就完成了 socket() + bind() + listen() 三件事

    // 事件循环的引用（保存构造时传进来的那个 ioc）
    net::io_context& _ioc;

    // 专门用来「接收新连接」的 socket。
    // 有新客户端连进来时，accept 会把这个 socket 变成与该客户端通信的通道；
    // 随后它被 std::move 交给 HttpConnection，CServer 里这个再换新的一轮继续用。
    // 为什么不用引用？因为引用必须在构造函数初始化列表里绑定、之后不能改绑；
    // 这里需要反复重新赋值，所以直接用对象。
	tcp::socket   _socket;//引用必须在构造函数初始化列表绑定对象，不能后期重新绑定.所以不用引用，直接用对象就行了。
};
