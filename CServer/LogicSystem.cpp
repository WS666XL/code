// ============================================================================
// LogicSystem.cpp —— 路由中心的三个函数实现
// ============================================================================

#include "LogicSystem.h"
#include"HttpConnection.h"

// ---------- 注册一个 GET 接口 ----------
// 把 (URL, 处理函数) 这一对塞进 map。
// 用 insert 而不是 []：insert 遇到重复 key 会插入失败（保留先注册的），
// 而 [] 会直接覆盖掉旧的处理函数。
void LogicSystem::RegGet(std::string url, HttpHandler handler) {
    _get_handlers.insert(make_pair(url, handler));
}

// ---------- 构造函数：在这里登记所有 GET 接口 ----------
// 因为 LogicSystem 是单例，整个程序中这个构造函数只会被执行一次，
// 所以路由表只会被初始化一次。想加新接口，就在这里加一行 RegGet 即可。
LogicSystem::LogicSystem() {
    // 注册一个测试接口：访问 http://127.0.0.1:8080/get_test 就会命中它
    RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
        // lambda 表达式（匿名函数）作为处理函数：
        // 参数 connection 就是当前这个客户端连接，
        // beast::ostream(...) 往响应的 body 里写字符串，这些内容最终会回到浏览器。
        // ▲ 这里能访问 connection->_response 这个私有成员，
        //   是因为 HttpConnection 把 LogicSystem 声明成了 friend（友元）。
        beast::ostream(connection->_response.body()) << "receive get_test req";
        });

    // ★ 想加新接口？照抄上面就行，例如：
    // RegGet("/hello", [](std::shared_ptr<HttpConnection> connection) {
    //     beast::ostream(connection->_response.body()) << "hello world";
    //     });
}

// ---------- 析构函数 ----------
// 【为什么必须有它？】
// 头文件里声明了 ~LogicSystem(); 就必须在某个 .cpp 里给出实现，
// 否则链接时会报 LNK2019「无法解析的外部符号」。
// 原因是单例内部用 shared_ptr<LogicSystem> 保存实例，
// 而 shared_ptr 销毁对象时需要调用析构函数，链接器必须能找到它的定义。
// 这里不需要额外清理工作，写成空函数体即可。
LogicSystem::~LogicSystem() {
}

// ---------- 分发：根据 URL 找到并执行对应的处理函数 ----------
bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con) {
    // find 找不到时返回 end()，说明这个 URL 从来没有注册过
    if (_get_handlers.find(path) == _get_handlers.end()) {
        return false;   // 调用方（HttpConnection::HandleReq）收到 false 就返回 404
    }

    // 找到了：通过 [] 取出这个函数对象，加 () 直接调用它，
    // 并把连接对象 con 传进去，让函数往响应里写内容
    _get_handlers[path](con);
    return true;        // 返回 true，调用方会补上 200 OK 并把响应发出去
}