// ============================================================================
// HttpConnection.h —— 「一个客户端连接」的封装类
//
// 每来一个客户端，CServer 就会 new 一个 HttpConnection 出来，
// 这个类负责这个客户端的完整生命周期：
//     读请求 → 解析 → 交给业务层处理 → 写回响应 → 关闭连接
//
// 生命周期怎么保证？
//   同样继承 enable_shared_from_this，每个异步回调里都捕获 shared_from_this()，
//   只要还有回调没执行完，对象就不会被析构（引用计数 > 0）。
// ============================================================================

#pragma once
#include "const.h"

class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
    // friend（友元）：允许 LogicSystem 直接访问本类的私有成员，
    // 这样业务处理函数才能往本对象的 _response（响应报文）里写数据。
    friend class LogicSystem;

public:
    // 构造函数：接收一个 socket（从 CServer 那里 move 过来的，代表与某个客户端的通路）
    HttpConnection(tcp::socket socket);

    // 启动这个连接的处理流程（内部发起「异步读请求」）
    void Start();

private:
    void CheckDeadline();//超时检测：60 秒还没搞定的连接就强制关掉，防止资源被占着不放
	void WriteResponse();//写入响应：把 _response 里准备好的数据真正发给客户端
	void HandleReq();//处理请求：解析请求方法（GET/POST），分发给 LogicSystem 对应的处理函数

    // 与本客户端通信的套接字（收发数据都靠它）
    tcp::socket  _socket;

    // Beast读缓冲区，初始8192字节
    // 从网络读到的数据先放这里；不够用时 Beast 会自动扩容
    beast::flat_buffer  _buffer{ 8192 };

    // HTTP请求对象，dynamic_body：body大小动态可变（因为事先不知道请求有多大）
    // 读完后可以从它身上取：方法(GET/POST)、URL(target)、头部字段、body
    http::request<http::dynamic_body> _request;

    // HTTP响应对象
    // 处理请求时把要返回的内容写进它的 body，再设置状态码和头部，最后由 WriteResponse 发出去
    http::response<http::dynamic_body> _response;

    // 超时定时器：绑定在 _socket 的 executor 上，超时时间 60 秒。
    // 调用 async_wait 挂上等待，60 秒后如果还没被 cancel，回调就会把 socket 关掉。
    net::steady_timer deadline_{
        _socket.get_executor(), std::chrono::seconds(60) };

};