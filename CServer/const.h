// ============================================================================
// const.h —— 整个项目的「公共头文件」
//
// 作用有两个：
//   1) 把所有源文件都要用到的 Boost 库、STL 头文件集中放在这里，
//      别的 .h / .cpp 只要 #include "const.h" 就够了；
//   2) 统一定义 4 个命名空间「小名」，后面写代码时直接写
//      beast:: / http:: / net:: / tcp:: ，不用每次写一长串全名。
// ============================================================================

#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include<iostream>
#include<memory>
#include "Singleton.h"
#include <functional>
#include <map>

// ---------- 下面 4 行只是给命名空间起「小名」，不是定义新类型 ----------

// beast：Boost.Beast —— 建立在 asio 之上的 HTTP / WebSocket 协议库。
//   asio 只管「收发字节」，beast 管「把字节解析成 HTTP 报文 / 把报文序列化成字节」。
//   常用：beast::flat_buffer（读缓冲区）、beast::error_code（错误码）、beast::ostream
namespace beast = boost::beast;         // from <boost/beast.hpp>

// http：Beast 里的 HTTP 子模块。
//   常用：http::request（请求）、http::response（响应）、
//        http::verb::get / post（请求方法）、http::status::ok / not_found（状态码）、
//        http::dynamic_body（长度可变的报文体）
namespace http = beast::http;           // from <boost/beast/http.hpp>

// net：Boost.Asio —— 真正的网络与异步 I/O 核心。
//   常用：net::io_context（事件循环 / 任务队列）、net::steady_timer（定时器）、
//        async_read / async_write / async_accept（各种异步操作）
namespace net = boost::asio;            // from <boost/asio.hpp>

// tcp：TCP 协议相关类型。
//   常用：tcp::socket（连接套接字，用来收发数据）、
//        tcp::acceptor（监听器，用来接受新连接）、
//        tcp::endpoint（IP地址 + 端口的组合）
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
