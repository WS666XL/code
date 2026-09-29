#pragma once
#include"const.h"

class RedisConPool {
public:
    RedisConPool(size_t poolSize, const char* host, int port, const char* pwd)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        for (size_t i = 0; i < poolSize_; ++i) {
            auto* context = redisConnect(host, port);
            if (context == nullptr || context->err != 0) {
                if (context != nullptr) {
                    redisFree(context);
                }
                continue;
            }

            auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd);
            if (reply->type == REDIS_REPLY_ERROR) {
                std::cout << "认证失败" << std::endl;
                //执行成功 释放redisCommand执行后返回的redisReply所占用的内存
                freeReplyObject(reply);
                continue;
            }

            //执行成功 释放redisCommand执行后返回的redisReply所占用的内存
            freeReplyObject(reply);
            std::cout << "认证成功" << std::endl;
            connections_.push(context);
        }

    }

    ~RedisConPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!connections_.empty()) {
            connections_.pop();
        }

    }

    redisContext* getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !connections_.empty();
            });
        //如果停止则直接返回空指针
        if (b_stop_) {
            return  nullptr;
        }
        auto* context = connections_.front();
        connections_.pop();
        return context;
    }

    void returnConnection(redisContext* context) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;
        }
        connections_.push(context);
        cond_.notify_one();
    }

    void Close() {
        b_stop_ = true;
        cond_.notify_all();
    }

private:
    std::atomic<bool> b_stop_;                       // 停止标志：置 true 后唤醒所有等待者并退出
    size_t poolSize_;                                // 池子大小（预建多少条连接）
    const char* host_;                               // Redis 地址
    int port_;                                       // Redis 端口
    std::queue<redisContext*> connections_;          // 空闲连接队列，借出/归还都走这里
    std::mutex mutex_;                               // 保护队列的互斥锁
    std::condition_variable cond_;                   // 池空时排队等待，归还时唤醒
};
// RedisMgr —— Redis 逻辑层单例：对外是一堆 Redis 命令，内部靠 RedisConPool 借还连接
class RedisMgr : public Singleton<RedisMgr>,public std::enable_shared_from_this<RedisMgr>
{
    friend class Singleton<RedisMgr>;
public:
    ~RedisMgr();
    bool Get(const std::string& key, std::string& value);                    // 取 key 的值（通过 value 带出），key 不存在返回 false
    bool Set(const std::string& key, const std::string& value);              // 设置 key 的值
    bool Auth(const std::string& password);                                  // 用密码认证 Redis 连接
    bool LPush(const std::string& key, const std::string& value);            // 列表：从左侧插入
    bool LPop(const std::string& key, std::string& value);                   // 列表：从左侧弹出（空列表返回 false）
    bool RPush(const std::string& key, const std::string& value);            // 列表：从右侧插入
    bool RPop(const std::string& key, std::string& value);                   // 列表：从右侧弹出（空列表返回 false）
    bool HSet(const std::string& key, const std::string& hkey, const std::string& value);   // 哈希：设置字段值
    bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);     // 哈希：二进制安全版（长度显式传入）
    std::string HGet(const std::string& key, const std::string& hkey);       // 哈希：取字段值，取不到返回空串
    bool Del(const std::string& key);                                        // 删除指定 key
    bool ExistsKey(const std::string& key);                                  // 判断 key 是否存在
    void Close();                                                            // 关闭连接池
private:
    RedisMgr();
    
	std::unique_ptr<RedisConPool> _con_pool;   // Redis 连接池：取连接 → 发命令 → 归还
};
