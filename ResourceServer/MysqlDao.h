#pragma once
#include "const.h"
#include "Logger.h"
#include <thread>
#include <atomic>
#include <condition_variable>
#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>
#include "data.h"
#include <memory>
#include <queue>
#include <mutex>
#include <chrono>

//MySQL 连接：真实连接 + 最近使用时间（心跳线程据此决定是否保活）
class SqlConnection {
public:
	SqlConnection(sql::Connection* con, int64_t lasttime) :_con(con), _last_oper_time(lasttime) {}
	std::unique_ptr<sql::Connection> _con;
	int64_t _last_oper_time;
};

//MySQL 连接池：预建 5 条连接，后台线程每 60 秒对闲置连接执行 SELECT 1 保活，
//断掉的连接直接重建。从 ChatServer 拷贝精简而来
class MySqlPool {
public:
	MySqlPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolSize)
		: url_(url), user_(user), pass_(pass), schema_(schema), poolSize_(poolSize), b_stop_(false) {
		try {
			for (int i = 0; i < poolSize_; ++i) {
				sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
				auto*  con = driver->connect(url_, user_, pass_);
				con->setSchema(schema_);
				// 获取当前时间戳
				auto currentTime = std::chrono::system_clock::now().time_since_epoch();
				// 将时间戳转换为秒
				long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
				pool_.push(std::make_unique<SqlConnection>(con, timestamp));
			}

			_check_thread = std::thread([this]() {
				while (!b_stop_) {
					checkConnection();
					std::this_thread::sleep_for(std::chrono::seconds(60));
				}
				});

			_check_thread.detach();
		}
		catch (sql::SQLException& e) {
			LLOG_INFO << "mysql pool init failed, error is " << e.what() << std::endl;
		}
	}

	void checkConnection() {
		std::lock_guard<std::mutex> guard(mutex_);
		int poolsize = pool_.size();
		auto currentTime = std::chrono::system_clock::now().time_since_epoch();
		long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
		for (int i = 0; i < poolsize; i++) {
			auto con = std::move(pool_.front());
			pool_.pop();
			Defer defer([this, &con]() {
				pool_.push(std::move(con));
				});

			if (timestamp - con->_last_oper_time < 5) {
				continue;
			}

			try {
				std::unique_ptr<sql::Statement> stmt(con->_con->createStatement());
				stmt->executeQuery("SELECT 1");
				con->_last_oper_time = timestamp;
			}
			catch (sql::SQLException& e) {
				LLOG_INFO << "Error keeping connection alive: " << e.what() << std::endl;
				// 重新创建连接并替换旧的连接
				sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
				auto* newcon = driver->connect(url_, user_, pass_);
				newcon->setSchema(schema_);
				con->_con.reset(newcon);
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
			con->_con->rollback();
			con->_con->setAutoCommit(true);
		}
		catch (...) {
			// 连接可能已损坏，忽略；仍归还以保持池大小
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
	std::string url_;
	std::string user_;
	std::string pass_;
	std::string schema_;
	int poolSize_;
	std::queue<std::unique_ptr<SqlConnection>> pool_;
	std::mutex mutex_;
	std::condition_variable cond_;
	std::atomic<bool> b_stop_;
	std::thread _check_thread;
};

// 秒传索引：md5 → 服务器上的物理文件（相对 output/static 的路径 + 大小）
struct FileObjectInfo {
	std::string file_path;   // 例如 "1002/a29e3a1d-....jpg"
	int64_t file_size = 0;
};

//DAO 层：资源服务器只做四件事——按 uid 读整行用户信息（写 Redis 缓存用）、
//更新 user.icon、按 message_id 读图片消息详情（推 gRPC 通知用）、
//更新图片消息上传状态。其他业务（注册/好友/文本聊天）都归 ChatServer 的 DAO 管
class MysqlDao
{
public:
	MysqlDao();
	~MysqlDao();
	std::shared_ptr<UserInfo> GetUser(int uid);
	bool UpdateHeadInfo(int uid, const std::string& icon);
	std::shared_ptr<ChatMessage> GetChatMsgById(int message_id);
	bool UpdateUploadStatus(int chat_message_id);

	// ===== 文件秒传索引：Redis 只是缓存，MySQL 是最终真相 =====
	bool GetFileObject(const std::string& md5, FileObjectInfo& info);
	bool UpsertFileObject(const std::string& md5, int64_t file_size,
		const std::string& rel_path);
	bool DeleteFileObject(const std::string& md5);
private:
	std::unique_ptr<MySqlPool> pool_;
};
