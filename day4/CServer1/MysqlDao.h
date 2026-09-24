#pragma once
#include"const.h"
#include <thread>
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>

// 一条 MySQL 连接的包装：连接本体 + 上次使用时间（保活线程靠它判断要不要探活）
class SqlConnection {
public:
    SqlConnection(sql::Connection* con, int64_t lasttime) :_con(con), _last_oper_time(lasttime) {}
    std::unique_ptr<sql::Connection> _con;   // 真正的一条 MySQL 连接，智能指针自动释放
    int64_t _last_oper_time;                 // 上次使用它的时间戳（秒）
};
class MySqlPool {
public:
    MySqlPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolSize)
        : url_(url), user_(user), pass_(pass), schema_(schema), poolSize_(poolSize), b_stop_(false) {
        try {
            for (int i = 0; i < poolSize_; ++i) {
                sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
                auto* con = driver->connect(url_, user_, pass_);
                con->setSchema(schema_);
                // 获取当前时间戳
                auto currentTime = std::chrono::system_clock::now().time_since_epoch();
                // 将时间戳转换为秒
                long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
                pool_.push(std::make_unique<SqlConnection>(con, timestamp));
                //std::cout << "mysql connection init success" << std::endl;
            }

            _check_thread = std::thread([this]() {
                while (!b_stop_) {
                    checkConnection();
					std::this_thread::sleep_for(std::chrono::seconds(60)); // 每隔60秒检查一次连接池,休眠60秒
                }
                });
			_check_thread.detach();//主线程**不等**子线程，两者各跑各的；子线程结束时自动释放资源
        }
        catch (sql::SQLException& e) {
            // 处理异常
            std::cout << "mysql pool init failed" <<e.what()<<std::endl;
        }
    }
    void checkConnection() {
        std::lock_guard<std::mutex> guard(mutex_);
        int poolsize = pool_.size();
        // 获取当前时间戳
        auto currentTime = std::chrono::system_clock::now().time_since_epoch();
        // 将时间戳转换为秒
        long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
        for (int i = 0; i < poolsize; i++) {
            auto con = std::move(pool_.front());
            pool_.pop();
            Defer defer([this, &con]() {
                pool_.push(std::move(con));
				});//Defer是一个自定义的类，用于在作用域结束时执行特定的操作，这里用于确保连接被重新放回连接池中

			if (timestamp - con->_last_oper_time < 5) {//如果距离上次操作时间小于5秒，则不执行查询，直接跳过
                continue;
            }

			try {//尝试执行一个简单的查询来保持连接活跃
                std::unique_ptr<sql::Statement> stmt(con->_con->createStatement());
                stmt->executeQuery("SELECT 1");
                con->_last_oper_time = timestamp;
                //std::cout << "execute timer alive query , cur is " << timestamp << std::endl;
            }
            catch (sql::SQLException& e) {
                std::cout << "Error keeping connection alive: " << e.what() << std::endl;
                // 重新创建连接并替换旧的连接
                sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
                auto* newcon = driver->connect(url_, user_, pass_);
                newcon->setSchema(schema_);
                con->_con.reset(newcon);//**接管传入的新对象 newcon**，智能指针现在指向 newcon
                con->_last_oper_time = timestamp;
            }
        }
    }
    std::unique_ptr<SqlConnection> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !pool_.empty(); });
        if (b_stop_) {
            return nullptr;
        }
        std::unique_ptr<SqlConnection> con(std::move(pool_.front()));
        pool_.pop();
        return con;
    }

    void returnConnection(std::unique_ptr<SqlConnection> con) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;
        }
        pool_.push(std::move(con));
        cond_.notify_one();
    }

    void Close() {
        b_stop_ = true;
        cond_.notify_all();
    }

    ~MySqlPool() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!pool_.empty()) {
            pool_.pop();
        }
    }

private:
    std::string url_;      // 连接地址（形如 "127.0.0.1:3308"）
    std::string user_;     // 数据库用户名
    std::string pass_;     // 数据库密码
    std::string schema_;   // 默认库名
    int poolSize_;         // 池子大小（预建多少条连接）
    std::queue<std::unique_ptr<SqlConnection>> pool_;   // 空闲连接队列，借出/归还都走这里
    std::mutex mutex_;                                  // 保护 pool_ 的互斥锁
    std::condition_variable cond_;                      // 池空时让调用者排队等待，归还时唤醒
    std::atomic<bool> b_stop_;                          // 停止标志：置 true 后唤醒所有等待者并退出
	std::thread _check_thread;// 线程检查连接池中连接的状态
};
// 从数据库读出的用户信息
struct UserInfo {
    std::string name;    // 用户名
    std::string email;   // 邮箱
    int uid;             // 业务用户号
    std::string pwd;     // 密码
};
// MysqlDao —— 数据访问层：负责拼 SQL / 调存储过程，连接池由它持有
class MysqlDao {
public:
    MysqlDao();
    ~MysqlDao(); 
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);   // 调存储过程 reg_user 插入用户：>0 为新 uid，0 = 用户名或邮箱已存在，-1 = 出错
   bool CheckEmail(const std::string& name, const std::string& email);                        // 查该用户名对应的邮箱是否一致
    bool UpdatePwd(const std::string& name, const std::string& newpwd);                       // 按用户名更新密码
    bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
    
private:
    std::unique_ptr<MySqlPool> pool_;   // 连接池：借一条连接 → 干活 → 归还
};