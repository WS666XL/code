// LogicSystem.h —— 业务逻辑分发中心（全局单例，URL → 处理函数 的路由表）
#pragma once
#include "const.h"

class HttpConnection;                             // 前向声明，避免头文件互相包含

// 处理函数类型：void (std::shared_ptr<HttpConnection>)
typedef std::function<void(std::shared_ptr<HttpConnection>)> HttpHandler;

class LogicSystem :public Singleton<LogicSystem>
{
    friend class Singleton<LogicSystem>;          // 构造函数为 private，需允许单例基类 new 本类

public:
    ~LogicSystem();

    bool HandleGet(std::string, std::shared_ptr<HttpConnection>);   // 查表执行；false = 未注册（404）
    bool HandlePost(std::string path, std::shared_ptr<HttpConnection> con);   // POST 版分发；false = 未注册（404）
    void RegGet(std::string, HttpHandler handler);                  // 注册 URL 与处理函数
    void RegPost(std::string url, HttpHandler handler);             // 注册 POST 接口（用 insert，同名不覆盖）
private:
    LogicSystem();                                // private：只能经 GetInstance() 获取；注册也在此完成

    std::map<std::string, HttpHandler> _post_handlers;  // POST 路由表（暂未使用）
    std::map<std::string, HttpHandler> _get_handlers;   // GET  路由表
};
