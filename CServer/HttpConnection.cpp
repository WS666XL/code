// ============================================================================
// HttpConnection.cpp —— 连接类的具体实现
// ============================================================================

#include "HttpConnection.h"
#include"LogicSystem.h"
// ---------- 构造函数 ----------
// 参数 socket 是用 std::move 移进来的（移动语义，避免拷贝，也符合 socket 不可复制的规则）
// 这里只负责「保存 socket」这一个小任务。
// 真正开始收数据的动作放在下面的 Start() 里，由 CServer 显式调用，职责更清晰。
HttpConnection::HttpConnection(tcp::socket socket) :_socket(std::move(socket))
{
    // 构造函数体保持为空是有意为之：只做初始化，不发起异步操作。
    // （注意：这里是空的，不代表功能丢了——发起异步读的代码就在下面的 Start() 里）
};

// ---------- 启动这条连接：发起异步读 ----------
// CServer 里创建完对象后紧接着就调用它：make_shared<HttpConnection>(...)->Start()
void HttpConnection::Start() {
    // 老套路：捕获指向自己的 shared_ptr，保证回调执行前对象活着
    auto self = shared_from_this();

    // 异步读取一个完整的 HTTP 请求：
    //   _socket ：从哪个连接读
    //   _buffer ：读到的数据暂存区
    //   _request：Beast 会自动把收到的字节解析成结构化的 HTTP 请求对象
    // 调用后立刻返回，等数据收完（收满一个完整的 HTTP 报文）才执行下面的 lambda
    http::async_read(_socket, _buffer, _request, [self](beast::error_code ec,
        std::size_t bytes_transferred) {
            try {
                // 出错（客户端断开、超时、协议错误等）就打印原因，直接放弃这条连接
                if (ec) {
                    std::cout << "http read err is " << ec.what() << std::endl;
                    return;
                }

                //处理读到的数据
                // ignore_unused：告诉编译器「我知道这个变量没用，别警告我」
                // （bytes_transferred 是本次收到的字节数，这里用不上）
                boost::ignore_unused(bytes_transferred);

                // ① 处理请求：解析并交给 LogicSystem 分发，生成响应内容
                self->HandleReq();

                // ② 启动超时检测：给这条连接挂一个 60 秒的倒计时
                self->CheckDeadline();
            }
            catch (std::exception& exp) {
                std::cout << "exception is " << exp.what() << std::endl;
            }
        }
    );
}
// ---------- 处理请求（本类的「大脑」） ----------
// 流程：先设置响应的基本信息 → 判断请求方法 → 交给 LogicSystem 分发 → 写回响应
void HttpConnection::HandleReq() {
    //设置版本：响应的 HTTP 版本要和请求保持一致（比如请求是 1.1，响应也是 1.1）
    _response.version(_request.version());
    //设置为短链接：处理完这一次就断开，不复用 TCP 连接（简单但开销略大）
    _response.keep_alive(false);

    // 判断请求方法是不是 GET（浏览器地址栏直接访问、点击链接，通常都是 GET）
    if (_request.method() == http::verb::get) {

        // 把「URL 路径」和「自己（本连接对象）」交给 LogicSystem 去分发处理。
        //   _request.target() 就是 URL 里域名后面那一串，比如访问
        //   http://127.0.0.1:8080/get_test ，target() 就是 "/get_test"
        //   传 shared_from_this() 是为了让业务函数能往本连接的 _response 里写返回内容
        //   （LogicSystem 是 HttpConnection 的友元，所以能访问私有的 _response）
        bool success = LogicSystem::GetInstance()->HandleGet(_request.target(), shared_from_this());

        // 没找到对应的处理函数 → 返回 404
        if (!success) {
            _response.result(http::status::not_found);              // 状态码 404
            _response.set(http::field::content_type, "text/plain"); // 告诉浏览器这是纯文本
            beast::ostream(_response.body()) << "url not found\r\n";// 写响应体内容
            WriteResponse();                                        // 真正发送出去
            return;
        }

        // 找到了 → 返回 200 OK
        // 注意：此时响应体已经被 LogicSystem 里注册的那个处理函数写好了
        _response.result(http::status::ok);
        _response.set(http::field::server, "GateServer");  // 响应头里加一句服务器标识
        WriteResponse();
        return;
    }

    // 小提示：现在只处理了 GET。如果是 POST 请求，这里什么都不会做，
    // 客户端会一直等不到响应，直到 60 秒超时被关掉。
    // 以后可以照着 GET 的写法，加一段 if (_request.method() == http::verb::post) { ... }
}

// ---------- 发送响应给客户端 ----------
void HttpConnection::WriteResponse() {
    auto self = shared_from_this();

    // HTTP 协议要求：响应头里必须写明 body 的长度，客户端才知道要收多少数据
    // （这行一定要放在 body 全部写完之后调用）
    _response.content_length(_response.body().size());

    // 异步写：把 _response（含状态行、头部、body）序列化成字节流发给客户端。
    // async_write 调用后立即返回，发完才会执行下面的回调
    http::async_write(
        _socket,
        _response,
        [self](beast::error_code ec, std::size_t)   // 第二个参数是发送的字节数，这里用不到，省略参数名
        {
            // 关闭「发送」方向：相当于告诉客户端"我的数据发完了"（TCP 半关闭）。
            // 但仍然可以接收客户端后续发来的数据，直到对方也关闭。
            self->_socket.shutdown(tcp::socket::shutdown_send, ec);

            // 响应已经发出，任务完成 → 取消超时定时器，别让它在 60 秒后把连接误杀
            self->deadline_.cancel();
        });
}

// ---------- 超时检测 ----------
// 作用：给这条连接设一个 60 秒的倒计时。
// 如果 60 秒内正常完成了响应，WriteResponse 里会 cancel 掉定时器，那么回调收到的 ec 是「已取消」；
// 如果 60 秒到了还没完成（比如客户端一直不发数据、或者像上面 POST 那种没处理的方法），
// 回调收到的 ec 为空，就说明真超时了，直接把 socket 关掉，回收资源。
void HttpConnection::CheckDeadline() {
    auto self = shared_from_this();

    // async_wait：异步等待定时器到期（或被取消）
    deadline_.async_wait(
        [self](beast::error_code ec)
        {
            // ec 为空 = 是「正常到期」而不是「被取消」 → 真超时了
            if (!ec)
            {
                // Close socket to cancel any outstanding operation.
                // 关掉 socket，顺带会取消掉这条连接上所有还在等待的异步操作
                self->_socket.close(ec);
            }
            // 如果 ec 非空（通常是 operation_aborted，即被 cancel），说明任务正常完成了，什么都不用做
        });
}