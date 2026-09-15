// HttpConnection.h —— 单个客户端连接的封装：读请求 → 处理 → 写响应
#pragma once
#include "const.h"


class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
    friend class LogicSystem;            // 允许业务层访问私有 _response

public:
    // socket 由 CServer move 进来，代表与该客户端的通路
    HttpConnection(tcp::socket socket);

    void Start();                        // 启动处理流程（发起异步读）

private:
    void PreParseGetParam();
    void CheckDeadline();//超时检测：60 秒未完成则关闭连接
	void WriteResponse();//写入响应：把 _response 发给客户端
	void HandleReq();//处理请求：解析方法并分发给 LogicSystem

    tcp::socket  _socket;                        // 与本客户端通信的套接字

    // 读缓冲区，初始 8192 字节，不够时 Beast 自动扩容
    beast::flat_buffer  _buffer{ 8192 };

    // HTTP 请求；dynamic_body 表示 body 长度可变，可取方法/URL/头部/body
    http::request<http::dynamic_body> _request;

    // HTTP 响应；处理时写入 body，最后交 WriteResponse 发送
    http::response<http::dynamic_body> _response;

    // 超时定时器，60 秒内未被 cancel 则关闭 socket
    net::steady_timer deadline_{
        _socket.get_executor(), std::chrono::seconds(60) };
    std::string _get_url;//GET 请求的路径
    std::unordered_map<std::string, std::string> _get_params;//GET 请求的查询参数 key/value
};
