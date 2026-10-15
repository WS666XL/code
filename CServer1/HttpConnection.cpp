// HttpConnection.cpp —— 连接类的具体实现
#include "HttpConnection.h"
#include"LogicSystem.h"
// 构造函数只保存 socket（move 进来，避免拷贝）；异步读放在 Start() 里由 CServer 显式调用
HttpConnection::HttpConnection(boost::asio::io_context& _ioc) :_socket(_ioc)
{
    // 函数体为空是有意为之：只做初始化，不发起异步操作
};

// 启动这条连接：发起异步读
void HttpConnection::Start() {
    auto self = shared_from_this();   // 捕获自身，保证回调期间对象存活

    // 异步读一个完整 HTTP 请求：_socket 读来源、_buffer 暂存、_request 自动解析
    // 收满一个完整报文后才执行回调
    http::async_read(_socket, _buffer, _request, [self](beast::error_code ec,
        std::size_t bytes_transferred) {
            try {
                // 出错（断开/超时/协议错误）则打印并放弃这条连接
                if (ec) {
                    std::cout << "http read err is " << ec.what() << std::endl;
                    return;
                }

                //处理读到的数据
                // 本次收到的字节数用不上，避免编译告警
                boost::ignore_unused(bytes_transferred);

                self->HandleReq();       // ① 处理请求，生成响应内容
                self->CheckDeadline();   // ② 启动 60 秒超时检测
            }
            catch (std::exception& exp) {
                std::cout << "exception is " << exp.what() << std::endl;
            }
        }
    );
}
//char 转为16进制
unsigned char ToHex(unsigned char x)
{
    return  x > 9 ? x + 55 : x + 48;
}
unsigned char FromHex(unsigned char x)
{
    unsigned char y;
    if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
    else if (x >= '0' && x <= '9') y = x - '0';
    else assert(0);
    return y;
}
std::string UrlEncode(const std::string& str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        //判断是否仅有数字和字母构成
        if (isalnum((unsigned char)str[i]) ||
            (str[i] == '-') ||
            (str[i] == '_') ||
            (str[i] == '.') ||
            (str[i] == '~'))
            strTemp += str[i];
        else if (str[i] == ' ') //为空字符
            strTemp += "+";
        else
        {
            //其他字符需要提前加%并且高四位和低四位分别转为16进制
            strTemp += '%';
            strTemp += ToHex((unsigned char)str[i] >> 4);
            strTemp += ToHex((unsigned char)str[i] & 0x0F);
        }
    }
    return strTemp;
}
std::string UrlDecode(const std::string& str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        //还原+为空
        if (str[i] == '+') strTemp += ' ';
        //遇到%将后面的两个字符从16进制转为char再拼接
        else if (str[i] == '%')
        {
            assert(i + 2 < length);
            unsigned char high = FromHex((unsigned char)str[++i]);
            unsigned char low = FromHex((unsigned char)str[++i]);
            strTemp += high * 16 + low;
        }
        else strTemp += str[i];
    }
    return strTemp;
}
void HttpConnection::PreParseGetParam() {
    // 提取 URI  
    auto uri = _request.target();
    // 查找查询字符串的开始位置（即 '?' 的位置）  
    auto query_pos = uri.find('?');
    if (query_pos == std::string::npos) {
        _get_url = uri;
        return;
    }

    _get_url = uri.substr(0, query_pos);
    std::string query_string = uri.substr(query_pos + 1);
    std::string key;
    std::string value;
    size_t pos = 0;
    while ((pos = query_string.find('&')) != std::string::npos) {
        auto pair = query_string.substr(0, pos);
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            key = UrlDecode(pair.substr(0, eq_pos)); // 假设有 url_decode 函数来处理URL解码  
            value = UrlDecode(pair.substr(eq_pos + 1));
            _get_params[key] = value;
        }
        query_string.erase(0, pos + 1);
    }
    // 处理最后一个参数对（如果没有 & 分隔符）  
    if (!query_string.empty()) {
        size_t eq_pos = query_string.find('=');
        if (eq_pos != std::string::npos) {
            key = UrlDecode(query_string.substr(0, eq_pos));
            value = UrlDecode(query_string.substr(eq_pos + 1));
            _get_params[key] = value;
        }
    }
}
// 处理请求：设置响应基本信息 → 按方法分发 → 写回响应
void HttpConnection::HandleReq() {
    _response.version(_request.version());   // 版本与请求保持一致
    _response.keep_alive(false);             // 短连接，处理完即断开

    if (_request.method() == http::verb::get) {   // 只处理 GET
        PreParseGetParam();
        // 用 URL 路径(target) 分发；传 shared_from_this() 便于业务函数写 _response
        bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());

        if (!success) {                                             // 未注册 → 404
            _response.result(http::status::not_found);              // 状态码 404
            _response.set(http::field::content_type, "text/plain"); // 纯文本
            beast::ostream(_response.body()) << "url not found\r\n";// 响应体
            WriteResponse();                                        // 发送
            return;
        }

        // 命中：body 已由注册的处理函数写好
        _response.result(http::status::ok);
        _response.set(http::field::server, "GateServer");   // 服务器标识
        WriteResponse();
        return;
    }
    if (_request.method() == http::verb::post) {
        bool success = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());
        if (!success) {
            _response.result(http::status::not_found);
            _response.set(http::field::content_type, "text/plain");
            beast::ostream(_response.body()) << "url not found\r\n";
            WriteResponse();
            return;
        }

        _response.result(http::status::ok);
        _response.set(http::field::server, "GateServer");
        WriteResponse();
        return;
    }
    // POST 未处理：客户端会一直等到 60 秒超时被关闭
}

// 发送响应给客户端
void HttpConnection::WriteResponse() {
    auto self = shared_from_this();

    // 必须放在 body 全部写完之后，告知客户端 body 长度
    _response.content_length(_response.body().size());

    // 异步写：把 _response 序列化发给客户端，发完才执行回调
    http::async_write(
        _socket,
        _response,
        [self](beast::error_code ec, std::size_t)   // 字节数用不到，省略参数名
        {
            // 半关闭发送方向：数据发完，仍可接收对方数据
            self->_socket.shutdown(tcp::socket::shutdown_send, ec);

            // 任务完成 → 取消超时定时器，避免 60 秒后被误关
            self->deadline_.cancel();
        });
}

// 超时检测：为连接设 60 秒倒计时
// 正常完成时 WriteResponse 会 cancel，回调 ec 非空；ec 为空说明真超时
void HttpConnection::CheckDeadline() {
    auto self = shared_from_this();

    deadline_.async_wait(   // 异步等待定时器到期或被取消
        [self](beast::error_code ec)
        {
            if (!ec)   // ec 为空 = 正常到期而非被取消 → 真超时
            {
                // Close socket to cancel any outstanding operation.
                // 关闭 socket，同时取消该连接上所有待处理的异步操作
                self->_socket.close(ec);
            }
        });
}

