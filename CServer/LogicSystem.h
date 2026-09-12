// ============================================================================
// LogicSystem.h —— 业务逻辑分发中心（全局单例）
//
// 它相当于一张「路由表（URL → 处理函数）」：
//   注册阶段：RegGet("/get_test", 处理函数)  把 URL 和对应的处理函数存进 map
//   运行阶段：HandleGet("/get_test", 连接)   查表找到函数并执行它
//
// 这样做的好处：网络层（HttpConnection）不用认识任何具体业务，
// 想加新接口只要 RegGet 注册一个就行，网络代码一行都不用改（解耦）。
// ============================================================================

#pragma once
#include "const.h"

// 前向声明：这里只需要「有个叫 HttpConnection 的类」就够了，
// 不用 include 它的头文件（避免头文件互相包含而死循环）
class HttpConnection;

// 给「处理函数」的类型起个别名 HttpHandler：
// 它是一个 std::function（可调用对象包装器），
// 形状是：返回值 void，参数是一个 shared_ptr<HttpConnection>
// 也就是说：任何形如 void f(std::shared_ptr<HttpConnection>) 的函数/lambda 都是 HttpHandler
typedef std::function<void(std::shared_ptr<HttpConnection>)> HttpHandler;

// 继承 Singleton<LogicSystem> → 本类成为单例，只能有一个实例
class LogicSystem :public Singleton<LogicSystem>
{
    // 把「单例模板类」声明为友元：
    // 因为构造函数是 private 的，必须允许 Singleton 在 GetInstance 里 new LogicSystem()
    friend class Singleton<LogicSystem>;

public:
    // 析构函数
    ~LogicSystem();

    // 【查表 + 执行】根据 URL 路径找到对应的 GET 处理函数并执行
    //  参数1：URL 路径，如 "/get_test"
    //  参数2：当前的连接对象（处理函数要通过它写响应内容）
    //  返回：true = 找到了并执行了；false = 这个 URL 没注册过（调用方会返回 404）
    bool HandleGet(std::string, std::shared_ptr<HttpConnection>);

    // 【注册】把一个 URL 和它的处理函数登记进路由表
    //  参数1：URL 路径
    //  参数2：处理函数（普通函数、lambda、函数对象都行）
    void RegGet(std::string, HttpHandler handler);

private:
    // 构造函数设为 private：外部不能自己 new，只能走 GetInstance()，保证只有一个实例。
    // 路由表的注册工作就放在这里做（对象一创建，接口就注册好了）。
    LogicSystem();

    // 两张路由表：key 是 URL 字符串，value 是对应的处理函数
    std::map<std::string, HttpHandler> _post_handlers;  // POST 请求的路由表（目前还没用到）
    std::map<std::string, HttpHandler> _get_handlers;   // GET  请求的路由表
};
