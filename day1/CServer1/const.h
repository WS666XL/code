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
enum ErrorCodes {
    Success = 0,
    Error_Json = 1001,  //Json解析错误
    RPCFailed = 1002,  //RPC请求错误
};
namespace beast = boost::beast;         // HTTP/WebSocket 协议库
namespace http = beast::http;           // HTTP 子模块
namespace net = boost::asio;            // 网络与异步 I/O 核心
using tcp = boost::asio::ip::tcp;       // TCP 类型

class ConfigMgr;
extern ConfigMgr gCfgMgr;