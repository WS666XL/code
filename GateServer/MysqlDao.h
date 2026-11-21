#pragma once
#include"const.h"
#include <thread>
#include "Logger.h"
#if __has_include(<jdbc/mysql_driver.h>)
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>
#else
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/exception.h>
#endif

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
                //LLOG_INFO << "mysql connection init success" << std::endl;
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
            LLOG_INFO << "mysql pool init failed" <<e.what()<<std::endl;
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
                //LLOG_INFO << "execute timer alive query , cur is " << timestamp << std::endl;
            }
            catch (sql::SQLException& e) {
                LLOG_INFO << "Error keeping connection alive: " << e.what() << std::endl;
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
        // 事务兜底清理：回滚任何未结束的事务（干净连接上是 no-op），再恢复 autocommit。
        // 顺序不能反——对开启的事务执行 set autocommit=1 会隐式提交。
        // 不清理的话，autocommit=0 的连接回池后被普通写操作借走，行锁永远无人提交释放，
        // 连接带锁挂起 → MySQL 死锁（只能重启服务端释放）。
		try {
			if (!con->_con->getAutoCommit()) {
				con->_con->rollback();
				con->_con->setAutoCommit(true);
			}
		}
        catch (...) {
            // 连接可能已损坏，忽略；仍归还以保持池大小（与原逻辑一致）
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
    std::string nick;    // 昵称
    std::string icon;    // 头像（qrc 资源路径，客户端直接加载）
    int sex;             // 性别
};
// MysqlDao —— 数据访问层：负责拼 SQL / 调存储过程，连接池由它持有
class MysqlDao {
public:
    MysqlDao();
    ~MysqlDao(); 
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    int RegUserTransaction(const std::string& name, const std::string& email, const std::string& pwd, const std::string& icon);
    bool CheckEmail(const std::string& name, const std::string& email);
    bool UpdatePwd(const std::string& name, const std::string& newpwd);
    bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
    bool TestProcedure(const std::string& email, int& uid, std::string& name);
private:
    std::unique_ptr<MySqlPool> pool_;   // 连接池：借一条连接 → 干活 → 归还
};