// LogicSystem.cpp —— 路由中心的函数实现
#include "LogicSystem.h"
#include"HttpConnection.h"
#include"VarifyGrpcClient.h"
// 注册 GET 接口：insert 遇重复 key 不覆盖（[] 会覆盖）
void LogicSystem::RegGet(std::string url, HttpHandler handler) {
    _get_handlers.insert(make_pair(url, handler));
}
// 注册 POST 接口：insert 遇重复 key 不覆盖（[] 会覆盖）
void LogicSystem::RegPost(std::string url, HttpHandler handler) {
    _post_handlers.insert(make_pair(url, handler));
}
// 构造函数：单例只执行一次，路由表只初始化一次；新增接口在此加一行
LogicSystem::LogicSystem() {
    // 测试接口：http://127.0.0.1:8080/get_test
    RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
        // 往响应 body 写字符串，最终回到浏览器（可访问私有 _response 是因 LogicSystem 为友元）
        beast::ostream(connection->_response.body()) << "receive get_test req"<<std::endl;
        int i = 0;
        for (auto& elem : connection->_get_params) {
            i++;
            beast::ostream(connection->_response.body()) << "param" << i << " key is " << elem.first;
            beast::ostream(connection->_response.body()) << ", " << " value is " << elem.second << std::endl;
        }
        });
    // ── POST /get_varifycode：前端提交邮箱 → 后端用 gRPC 找 VarifyServer 要验证码 → 以 JSON 回给前端
    // 这里是 HTTP 与 gRPC 的「交界处」：左边是 HTTP 请求体，右边会变成一次真实 RPC 调用
    RegPost("/get_varifycode", [](std::shared_ptr<HttpConnection> connection) {
        // 把 HTTP 请求体整个读成字符串（前端发来的 JSON）
        auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
        std::cout << "receive body is " << body_str << std::endl;
        connection->_response.set(http::field::content_type, "text/json");   // 告知浏览器本次回的是 JSON
        Json::Value root;         // 待返回的 JSON
        Json::Reader reader;      // JSON 解析器
        Json::Value src_root;     // 解析结果
        bool parse_success = reader.parse(body_str, src_root);
        if (!parse_success) {
            std::cout << "Failed to parse JSON data!" << std::endl;
            root["error"] = ErrorCodes::Error_Json;
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;
        }
        if(!src_root.isMember("email")) {          // 字段缺失（isMember 判存在，不能写成 src_root["email"] 直接判）
            std::cout << "Missing 'email' field in JSON data!" << std::endl;
            root["error"] = ErrorCodes::Error_Json;    // 1001
            std::string jsonstr = root.toStyledString();
            beast::ostream(connection->_response.body()) << jsonstr;
            return true;                                 // 返回 true 才会走 200 OK，错误码靠 body 里的 error 表达
		}
        auto email = src_root["email"].asString();
        // ↓ 关键一步：本次 HTTP 请求在这里被转成一次 gRPC 调用
        // GetInstance() 拿全局唯一客户端，GetVarifyCode() 阻塞等 VarifyServer 返回
        // 调用链：GetVarifyCode → 填 GetVarifyReq → Channel 发 HTTP/2 → 服务端处理 → 反序列化进 GetVarifyRsp
        GetVarifyRsp rsp = VerifyGrpcClient::GetInstance()->GetVarifyCode(email);
        std::cout << "email is " << email << std::endl;
        root["error"] = 0;                           // 0 = 成功（rsp.error 目前未回填，后续可改成 root["error"] = rsp.error）
        root["email"] = src_root["email"];
        std::string jsonstr = root.toStyledString();
        beast::ostream(connection->_response.body()) << jsonstr;
        return true;
        });
}

// 析构函数：头文件已声明就必须有定义，否则链接报 LNK2019 无法解析的外部符号
LogicSystem::~LogicSystem() {
}

// 分发：按 URL 找到并执行对应处理函数
bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con) {
    if (_get_handlers.find(path) == _get_handlers.end()) {
        return false;   // find 返回 end() 表示从未注册 → 调用方返回 404
    }

    // 取出函数对象并调用，传入连接对象以便写入响应
    _get_handlers[path](con);
    return true;        // 调用方补 200 OK 并把响应发出去
}
bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> con) {
    if (_post_handlers.find(path) == _post_handlers.end()) {
        return false;
    }

    _post_handlers[path](con);
    return true;
}
