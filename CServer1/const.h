// const.h —— 公共头文件：集中包含 + 命名空间短别名
#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include<iostream>
#include<memory>
#include "Singleton.h"
#include <functional>
#include <map>
#include <unordered_map>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include<atomic>
#include<queue>
#include<mutex>
#include<condition_variable>
#include"hiredis.h"
#include<assert.h>


namespace beast = boost::beast;         // HTTP/WebSocket 协议库
namespace http = beast::http;           // HTTP 子模块
namespace net = boost::asio;            // 网络与异步 I/O 核心
using tcp = boost::asio::ip::tcp;       // TCP 类型
enum ErrorCodes {

    Error_Json = 1001,//Json解析错误
    RPCFailed = 1002,//RPC请求错误
    VarifyExpired = 1003,//验证码过期
    VarifyCodeErr = 1004,//验证码错误
    UserExist = 1005,//用户已经存在
    PasswdErr = 1006,//密码错误
    EmailNotMatch = 1007,//邮箱不匹配
    PasswdUpFailed = 1008,//更新密码失败
    PasswdInvalid = 1009,//密码更新失败
};
class Defer {
public:
    //接受一个函数对象
    Defer(std::function<void()> func) : func_(func) {}
    ~Defer()
    {
        func_();
    }
private:
	std::function<void()> func_;
};
#define CODEPREFIX "code_"