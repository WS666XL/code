#include "RedisMgr.h"
#include"ConfigMgr.h"
// 关闭 Redis 连接
void RedisMgr::Close()
{
    // 释放 Redis 连接对象
    _con_pool->Close();
}

RedisMgr::RedisMgr() {
    auto& gCfgMgr = ConfigMgr::Inst();
    auto host = gCfgMgr["Redis"]["Host"];
    auto port = gCfgMgr["Redis"]["Port"];
    auto pwd = gCfgMgr["Redis"]["Passwd"];
    _con_pool.reset(new RedisConPool(5, host.c_str(), atoi(port.c_str()), pwd.c_str()));
}
RedisMgr::~RedisMgr() {
    Close();
}

bool RedisMgr::Get(const std::string& key, std::string& value)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    auto reply = (redisReply*)redisCommand(connect, "GET %s", key.c_str());
    if (reply == NULL) {
        std::cout << "[ GET  " << key << " ] failed" << std::endl;
        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    if (reply->type != REDIS_REPLY_STRING) {
        std::cout << "[ GET  " << key << " ] failed" << std::endl;
        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    value = reply->str;
    freeReplyObject(reply);

    std::cout << "Succeed to execute command [ GET " << key << "  ]" << std::endl;
    _con_pool->returnConnection(connect);
    return true;
}

// 向 Redis 中设置 key-value
bool RedisMgr::Set(const std::string& key, const std::string& value)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 向 Redis 发送 SET 命令
    auto reply =(redisReply*)redisCommand(
            connect,
            "SET %s %s",
            key.c_str(),
            value.c_str()
        );

    // Redis 命令执行失败
    if (NULL ==reply)
    {
        std::cout << "Execut command [ SET "
            << key << "  " << value
            << " ] failure ! "
            << std::endl;

        // 释放返回结果
        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    // 判断 Redis 是否返回 OK，只有返回 OK 才表示 SET 成功
    if (!(reply->type == REDIS_REPLY_STATUS &&
        (strcmp(reply->str, "OK") == 0 ||
            strcmp(reply->str, "ok") == 0)))
    {
        std::cout << "Execut command [ SET "
            << key << "  " << value
            << " ] failure ! "
            << std::endl;

        // 释放返回结果
        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    // 执行成功，释放 Redis 返回结果
    freeReplyObject(reply);

    std::cout << "Execut command [ SET "
        << key << "  " << value
        << " ] success ! "
        << std::endl;
    _con_pool->returnConnection(connect);
    return true;
}


// 使用密码认证 Redis
bool RedisMgr::Auth(const std::string& password)
{

    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 向 Redis 发送 AUTH 命令
   auto reply =
        (redisReply*)redisCommand(
            connect,
            "AUTH %s",
            password.c_str()
        );

    // Redis 返回错误，说明认证失败
    if (reply->type == REDIS_REPLY_ERROR)
    {
        std::cout << "认证失败" << std::endl;

        // 释放返回结果
        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }
    else
    {
        // 认证成功，释放返回结果
        freeReplyObject(reply);
       
        std::cout << "认证成功" << std::endl;
        _con_pool->returnConnection(connect);
        return true;
    }
}
// 从列表左侧插入元素
bool RedisMgr::LPush(const std::string& key, const std::string& value)
{

    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 执行 LPUSH 命令，将 value 插入列表左侧
   auto reply = (redisReply*)redisCommand(
        connect,
        "LPUSH %s %s",
        key.c_str(),
        value.c_str());

    // 命令执行失败
    if (NULL ==reply)
    {
        std::cout << "Execut command [ LPUSH "
            << key << "  " << value
            << " ] failure ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    // LPUSH 返回整数，表示插入后列表的长度
    // 长度小于等于 0 说明执行失败
    if (reply->type != REDIS_REPLY_INTEGER ||
       reply->integer <= 0)
    {
        std::cout << "Execut command [ LPUSH "
            << key << "  " << value
            << " ] failure ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    std::cout << "Execut command [ LPUSH "
        << key << "  " << value
        << " ] success ! "
        << std::endl;

    // 释放 Redis 返回结果
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}


// 从列表左侧弹出一个元素
bool RedisMgr::LPop(const std::string& key, std::string& value)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 执行 LPOP 命令
   auto reply = (redisReply*)redisCommand(
        connect,
        "LPOP %s ",
        key.c_str());

    // 返回空或者列表不存在/没有元素
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL)
    {
        std::cout << "Execut command [ LPOP "
            << key
            << " ] failure ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    // 获取 Redis 返回的字符串
    value = reply->str;

    std::cout << "Execut command [ LPOP "
        << key
        << " ] success ! "
        << std::endl;

    // 释放返回结果
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}


// 从列表右侧插入元素
bool RedisMgr::RPush(const std::string& key, const std::string& value)
{

    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 执行 RPUSH 命令，将 value 插入列表右侧
   auto reply = (redisReply*)redisCommand(
        connect,
        "RPUSH %s %s",
        key.c_str(),
        value.c_str());

    // 命令执行失败
    if (NULL ==reply)
    {
        std::cout << "Execut command [ RPUSH "
            << key << "  " << value
            << " ] failure ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    // RPUSH 返回整数，表示插入后列表的长度
    if (reply->type != REDIS_REPLY_INTEGER ||
       reply->integer <= 0)
    {
        std::cout << "Execut command [ RPUSH "
            << key << "  " << value
            << " ] failure ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    std::cout << "Execut command [ RPUSH "
        << key << "  " << value
        << " ] success ! "
        << std::endl;

    // 释放 Redis 返回结果
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}


// 从列表右侧弹出一个元素
bool RedisMgr::RPop(const std::string& key, std::string& value)
{

    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 执行 RPOP 命令
   auto reply = (redisReply*)redisCommand(
        connect,
        "RPOP %s ",
        key.c_str());

    // 没有返回结果或者列表为空
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL)
    {
        std::cout << "Execut command [ RPOP "
            << key
            << " ] failure ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    // 保存弹出的数据
    value = reply->str;

    std::cout << "Execut command [ RPOP "
        << key
        << " ] success ! "
        << std::endl;

    // 释放返回结果
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}


// 设置 Hash 中的字段和值
bool RedisMgr::HSet(const std::string& key,
    const std::string& hkey,
    const std::string& value)
{

    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 执行 HSET 命令
    // key 是 Hash 名称，hkey 是字段，value 是字段对应的值
   auto reply = (redisReply*)redisCommand(
        connect,
        "HSET %s %s %s",
        key.c_str(),
        hkey.c_str(),
        value.c_str());

    // Redis 没有返回结果或者返回类型不正确
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER)
    {
        std::cout << "Execut command [ HSet "
            << key << "  "
            << hkey << "  "
            << value
            << " ] failure ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    std::cout << "Execut command [ HSet "
        << key << "  "
        << hkey << "  "
        << value
        << " ] success ! "
        << std::endl;

    // 释放返回结果
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}


// 使用 redisCommandArgv 设置 Hash 字段
// 这种方式可以更安全地处理包含特殊字符的数据
bool RedisMgr::HSet(const char* key,
    const char* hkey,
    const char* hvalue,
    size_t hvaluelen)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 保存 Redis 命令的参数
    const char* argv[4];
    size_t argvlen[4];

    argv[0] = "HSET";
    argvlen[0] = 4;

    argv[1] = key;
    argvlen[1] = strlen(key);

    argv[2] = hkey;
    argvlen[2] = strlen(hkey);

    argv[3] = hvalue;
    argvlen[3] = hvaluelen;


    
    // 使用参数数组执行 HSET
   auto reply = (redisReply*)redisCommandArgv(
        connect,
        4,
        argv,
        argvlen);

    // 判断执行结果
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER)
    {
        std::cout << "Execut command [ HSet "
            << key << "  "
            << hkey << "  "
            << hvalue
            << " ] failure ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    std::cout << "Execut command [ HSet "
        << key << "  "
        << hkey << "  "
        << hvalue
        << " ] success ! "
        << std::endl;

    // 释放 Redis 返回结果
    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}


// 获取 Hash 中某个字段对应的值
std::string RedisMgr::HGet(const std::string& key,
    const std::string& hkey)
{
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return "";
    }
    const char* argv[3];
    size_t argvlen[3];

    // 组装 HGET 命令
    argv[0] = "HGET";
    argvlen[0] = 4;

    argv[1] = key.c_str();
    argvlen[1] = key.length();

    argv[2] = hkey.c_str();
    argvlen[2] = hkey.length();

    
    // 执行 HGET
   auto reply = (redisReply*)redisCommandArgv(
        connect,
        3,
        argv,
        argvlen);

    // 没找到对应字段
    if (reply == nullptr ||
       reply->type == REDIS_REPLY_NIL)
    {
        freeReplyObject(reply);

        std::cout << "Execut command [ HGet "
            << key << " "
            << hkey
            << " ] failure ! "
            << std::endl;
        _con_pool->returnConnection(connect);
        return "";
    }

    // 获取 Redis 返回的字符串
    std::string value =reply->str;

    // 释放返回结果
    freeReplyObject(reply);

    std::cout << "Execut command [ HGet "
        << key << " "
        << hkey
        << " ] success ! "
        << std::endl;
    _con_pool->returnConnection(connect);
    return value;
}


// 删除 Redis 中指定的 key
bool RedisMgr::Del(const std::string& key)
{

    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 执行 DEL 命令
   auto reply = (redisReply*)redisCommand(
        connect,
        "DEL %s",
        key.c_str());

    // DEL 返回整数，表示删除的 key 数量
    if (reply == nullptr ||
       reply->type != REDIS_REPLY_INTEGER)
    {
        std::cout << "Execut command [ Del "
            << key
            << " ] failure ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    std::cout << "Execut command [ Del "
        << key
        << " ] success ! "
        << std::endl;

    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}


// 判断 key 是否存在
bool RedisMgr::ExistsKey(const std::string& key)
{

    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        _con_pool->returnConnection(connect);
        return false;
    }
    // 执行 EXISTS 命令
   auto reply = (redisReply*)redisCommand(
        connect,
        "exists %s",
        key.c_str());

    // 返回 0 表示 key 不存在
    if (reply == nullptr ||
       reply->type != REDIS_REPLY_INTEGER ||
       reply->integer == 0)
    {
        std::cout << "Not Found [ Key "
            << key
            << " ] ! "
            << std::endl;

        freeReplyObject(reply);
        _con_pool->returnConnection(connect);
        return false;
    }

    std::cout << "Found [ Key "
        << key
        << " ] exists ! "
        << std::endl;

    freeReplyObject(reply);
    _con_pool->returnConnection(connect);
    return true;
}

