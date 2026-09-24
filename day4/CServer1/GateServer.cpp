// GateServer.cpp —— 程序入口（main 所在）：事件循环驱动
#include"const.h"
#include"CServer.h"
#include"ConfigMgr.h"
#include"RedisMgr.h"
void TestRedis() {
    //连接redis 需要启动才可以进行连接
//redis默认监听端口为6387 可以再配置文件中修改
    redisContext* c = redisConnect("127.0.0.1", 6380);
    if (c->err)
    {
        printf("Connect to redisServer faile:%s\n", c->errstr);
        redisFree(c);        return;
    }
    printf("Connect to redisServer Success\n");

    std::string redis_password = "123456";
    redisReply* r = (redisReply*)redisCommand(c, "AUTH %s", redis_password.c_str());
    if (r->type == REDIS_REPLY_ERROR) {
        printf("Redis认证失败！\n");
    }
    else {
        printf("Redis认证成功！\n");
    }

    //为redis设置key
    const char* command1 = "set stest1 value1";

    //执行redis命令行
    r = (redisReply*)redisCommand(c, command1);

    //如果返回NULL则说明执行失败
    if (NULL == r)
    {
        printf("Execut command1 failure\n");
        redisFree(c);        return;
    }

    //如果执行失败则释放连接
    if (!(r->type == REDIS_REPLY_STATUS && (strcmp(r->str, "OK") == 0 || strcmp(r->str, "ok") == 0)))
    {
        printf("Failed to execute command[%s]\n", command1);
        freeReplyObject(r);
        redisFree(c);        return;
    }

    //执行成功 释放redisCommand执行后返回的redisReply所占用的内存
    freeReplyObject(r);
    printf("Succeed to execute command[%s]\n", command1);

    const char* command2 = "strlen stest1";
    r = (redisReply*)redisCommand(c, command2);

    //如果返回类型不是整形 则释放连接
    if (r->type != REDIS_REPLY_INTEGER)
    {
        printf("Failed to execute command[%s]\n", command2);
        freeReplyObject(r);
        redisFree(c);        return;
    }

    //获取字符串长度
    int length = r->integer;
    freeReplyObject(r);
    printf("The length of 'stest1' is %d.\n", length);
    printf("Succeed to execute command[%s]\n", command2);

    //获取redis键值对信息
    const char* command3 = "get stest1";
    r = (redisReply*)redisCommand(c, command3);
    if (r->type != REDIS_REPLY_STRING)
    {
        printf("Failed to execute command[%s]\n", command3);
        freeReplyObject(r);
        redisFree(c);        return;
    }
    printf("The value of 'stest1' is %s\n", r->str);
    freeReplyObject(r);
    printf("Succeed to execute command[%s]\n", command3);

    const char* command4 = "get stest2";
    r = (redisReply*)redisCommand(c, command4);
    if (r->type != REDIS_REPLY_NIL)
    {
        printf("Failed to execute command[%s]\n", command4);
        freeReplyObject(r);
        redisFree(c);        return;
    }
    freeReplyObject(r);
    printf("Succeed to execute command[%s]\n", command4);

    //释放连接资源
    redisFree(c);

}
void TestRedisMgr() {
    
    assert(RedisMgr::GetInstance()->Set("blogwebsite", "llfc.club"));
    std::string value = "";
    assert(RedisMgr::GetInstance()->Get("blogwebsite", value));
    assert(RedisMgr::GetInstance()->Get("nonekey", value) == false);
    assert(RedisMgr::GetInstance()->HSet("bloginfo", "blogwebsite", "llfc.club"));
    assert(RedisMgr::GetInstance()->HGet("bloginfo", "blogwebsite") != "");
    assert(RedisMgr::GetInstance()->ExistsKey("bloginfo"));
    assert(RedisMgr::GetInstance()->Del("bloginfo"));
    assert(RedisMgr::GetInstance()->Del("bloginfo"));
    assert(RedisMgr::GetInstance()->ExistsKey("bloginfo") == false);
    assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue1"));
    assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue2"));
    assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue3"));
    assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
    assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
    assert(RedisMgr::GetInstance()->LPop("lpushkey1", value));
    assert(RedisMgr::GetInstance()->LPop("lpushkey2", value) == false);
}
int main()
{
    //TestRedisMgr();
   auto &gCfgMgr = ConfigMgr::Inst() ;  
    std::string gate_port_str = gCfgMgr["GateServer"]["Port"];
    unsigned short gate_port = atoi(gate_port_str.c_str());   // 从 config.ini 读出的端口
    try
    {
        // ① 监听端口：访问 http://127.0.0.1:8080 即可连上
        // 【注意】这里没用上面的 gate_port，端口是写死的 8080。
        // 想让 config.ini 生效，把这行改成 unsigned short port = gate_port; 即可（此处只加注释，未改代码）
        unsigned short port = static_cast<unsigned short>(8080);

        // ② 创建事件循环（1 表示只用 1 个线程）；必须 ioc.run() 异步任务才会执行
        net::io_context ioc{ 1 };

        // ③ 信号监听，实现优雅退出：SIGINT = Ctrl+C，SIGTERM = kill
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);

        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number) {

            if (error) {
                return;
            }
            ioc.stop();   // 停止事件循环 → run() 返回 → 进程正常退出
            });

        // ④ 创建服务器并启动监听（CServer 需被 shared_ptr 管理才能用 shared_from_this）
        std::make_shared<CServer>(ioc, port)->Start();
		std::cout << "Server started on port " << port << std::endl;
        // ⑤ 启动事件循环（阻塞在此），不断执行到期的异步任务回调
        ioc.run();
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;   // 异常兜底，返回非 0
        return EXIT_FAILURE;
    }
}


