#include "MysqlDao.h"
#include "Logger.h"
#include "ConfigMgr.h"

MysqlDao::MysqlDao()
{
	auto & cfg = ConfigMgr::Inst();
	const auto& host = cfg["Mysql"]["Host"];
	const auto& port = cfg["Mysql"]["Port"];
	const auto& pwd = cfg["Mysql"]["Passwd"];
	const auto& schema = cfg["Mysql"]["Schema"];
	const auto& user = cfg["Mysql"]["User"];
	pool_.reset(new MySqlPool(host+":"+port, user, pwd,schema, 5));
}

MysqlDao::~MysqlDao(){
	pool_->Close();
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}
		// 准备调用存储过程
		std::unique_ptr < sql::PreparedStatement > stmt(con->_con->prepareStatement("CALL reg_user(?,?,?,@result)"));
		// 设置输入参数
		stmt->setString(1, name);
		stmt->setString(2, email);
		stmt->setString(3, pwd);

		// 由于PreparedStatement不直接支持注册输出参数，我们需要使用会话变量或其他方法来获取输出参数的值

		  // 执行存储过程
		stmt->execute();
		// 如果存储过程设置了会话变量或有其他方式获取输出参数的值，你可以在这里执行SELECT查询来获取它们
	   // 例如，如果存储过程设置了一个会话变量@result来存储输出结果，可以这样获取：
	   std::unique_ptr<sql::Statement> stmtResult(con->_con->createStatement());
	  std::unique_ptr<sql::ResultSet> res(stmtResult->executeQuery("SELECT @result AS result"));
	  if (res->next()) {
	       int result = res->getInt("result");
	      LLOG_INFO << "Result: " << result << std::endl;
		  pool_->returnConnection(std::move(con));
		  return result;
	  }
	  pool_->returnConnection(std::move(con));
		return -1;
	}
	catch (sql::SQLException& e) {
		pool_->returnConnection(std::move(con));
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return -1;
	}
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}

		// 准备查询语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT email FROM user WHERE name = ?"));

		// 绑定参数
		pstmt->setString(1, name);

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

		// 遍历结果集
		while (res->next()) {
			LLOG_INFO << "Check Email: " << res->getString("email") << std::endl;
			if (email != res->getString("email")) {
				pool_->returnConnection(std::move(con));
				return false;
			}
			pool_->returnConnection(std::move(con));
			return true;
		}
		return true;
	}
	catch (sql::SQLException& e) {
		pool_->returnConnection(std::move(con));
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}

		// 准备查询语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE user SET pwd = ? WHERE name = ?"));

		// 绑定参数
		pstmt->setString(2, name);
		pstmt->setString(1, newpwd);

		// 执行更新
		int updateCount = pstmt->executeUpdate();

		LLOG_INFO << "Updated rows: " << updateCount << std::endl;
		pool_->returnConnection(std::move(con));
		return true;
	}
	catch (sql::SQLException& e) {
		pool_->returnConnection(std::move(con));
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}

bool MysqlDao::CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
	

		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE name = ?"));
		pstmt->setString(1, name); // 将username替换为你要查询的用户名

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::string origin_pwd = "";
		// 遍历结果集
		while (res->next()) {
			origin_pwd = res->getString("pwd");
			// 输出查询到的密码
			LLOG_INFO << "Password: " << origin_pwd << std::endl;
			break;
		}

		if (pwd != origin_pwd) {
			return false;
		}
		userInfo.name = name;
		userInfo.email = res->getString("email");
		userInfo.uid = res->getInt("uid");
		userInfo.pwd = origin_pwd;
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE uid = ?"));
		pstmt->setInt(1, uid); // 将uid替换为你要查询的uid

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<UserInfo> user_ptr = nullptr;
		// 遍历结果集
		while (res->next()) {
			user_ptr.reset(new UserInfo);
			user_ptr->pwd = res->getString("pwd");
			user_ptr->email = res->getString("email");
			user_ptr->name = res->getString("name");
			user_ptr->nick = res->getString("nick");
			user_ptr->desc = res->getString("desc");
			user_ptr->sex = res->getInt("sex");
			user_ptr->icon = res->getString("icon");
			user_ptr->uid = uid;
			break;
		}
		return user_ptr;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return nullptr;
	}
}
std::shared_ptr<UserInfo> MysqlDao::GetUser( std::string name) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE name = ?"));
		pstmt->setString(1, name); // 将uid替换为你要查询的uid

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<UserInfo> user_ptr = nullptr;
		// 遍历结果集
		while (res->next()) {
			user_ptr.reset(new UserInfo);
			user_ptr->pwd = res->getString("pwd");
			user_ptr->email = res->getString("email");
			user_ptr->name = res->getString("name");
			user_ptr->nick = res->getString("nick");
			user_ptr->desc = res->getString("desc");
			user_ptr->sex = res->getInt("sex");
			user_ptr->icon = res->getString("icon");
			user_ptr->uid = res->getInt("uid");
			break;
		}
		return user_ptr;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return nullptr;
	}
}
bool MysqlDao::AddFriendApply(const int& from, const int& to)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("INSERT INTO friend_apply (from_uid, to_uid) values (?,?) "
			// 重复申请（例如删好友后重新加）要把旧记录重置成未处理，
			// 否则 status 一直停在 1，对方客户端会当成"已处理过"忽略新申请
			"ON DUPLICATE KEY UPDATE status = 0"));
		pstmt->setInt(1, from); // from id
		pstmt->setInt(2, to);
		// 执行更新
		int rowAffected = pstmt->executeUpdate();
		if (rowAffected < 0) {
			return false;
		}
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}


	return true;
}
bool MysqlDao::GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});


	try {
		// 准备SQL语句, 根据起始id和限制条数返回列表
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("select apply.from_uid, apply.status, user.name, "
			"user.nick, user.sex, user.icon from friend_apply as apply join user on apply.from_uid = user.uid where apply.to_uid = ? "
			"and apply.id > ? and apply.status = 0 order by apply.id ASC LIMIT ? "));

		pstmt->setInt(1, touid); // 将uid替换为你要查询的uid
		pstmt->setInt(2, begin); // 起始id
		pstmt->setInt(3, limit); //偏移量
		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		// 遍历结果集
		while (res->next()) {
			auto name = res->getString("name");
			auto uid = res->getInt("from_uid");
			auto status = res->getInt("status");
			auto nick = res->getString("nick");
			auto sex = res->getInt("sex");
			auto icon = res->getString("icon");
			auto apply_ptr = std::make_shared<ApplyInfo>(uid, name, "", icon, nick, sex, status);
			applyList.push_back(apply_ptr);
		}
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}
bool MysqlDao::AuthFriendApply(const int& from, const int& to) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 准备SQL语句
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE friend_apply SET status = 1 "
			"WHERE from_uid = ? AND to_uid = ?"));
		//反过来的申请时from，验证时to
		pstmt->setInt(1, to); // from id
		pstmt->setInt(2, from);
		// 执行更新
		int rowAffected = pstmt->executeUpdate();
		if (rowAffected < 0) {
			return false;
		}
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}


	return true;
}

bool MysqlDao::AddFriend(const int& from, const int& to, std::string back_name) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {

		//开始事务
		con->_con->setAutoCommit(false);

		// 准备第一个SQL语句, 插入认证方好友数据
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("INSERT IGNORE INTO friend(self_id, friend_id, back) "
			"VALUES (?, ?, ?) "
		));
		//反过来的申请时from，验证时to
		pstmt->setInt(1, from); // from id
		pstmt->setInt(2, to);
		pstmt->setString(3, back_name);
		// 执行更新
		int rowAffected = pstmt->executeUpdate();
		if (rowAffected < 0) {
			con->_con->rollback();
			return false;
		}

		//准备第二个SQL语句，插入申请方好友数据
		std::unique_ptr<sql::PreparedStatement> pstmt2(con->_con->prepareStatement("INSERT IGNORE INTO friend(self_id, friend_id, back) "
			"VALUES (?, ?, ?) "
		));
		//反过来的申请时from，验证时to
		pstmt2->setInt(1, to); // from id
		pstmt2->setInt(2, from);
		pstmt2->setString(3, "");
		// 执行更新
		int rowAffected2 = pstmt2->executeUpdate();
		if (rowAffected2 < 0) {
			con->_con->rollback();
			return false;
		}

		// 提交事务
		con->_con->commit();
		LLOG_INFO << "addfriend insert friends success" << std::endl;

		return true;
	}
	catch (sql::SQLException& e) {
		// 如果发生错误，回滚事务
		if (con) {
			con->_con->rollback();
		}
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
	catch (std::exception& e) {
		// 非 SQL 异常也要回滚，防止带开事务的连接被归还连接池
		try { if (con) { con->_con->rollback(); } } catch (...) {}
		LLOG_ERROR << "AddFriend std::exception: " << e.what() << std::endl;
		return false;
	}
	catch (...) {
		try { if (con) { con->_con->rollback(); } } catch (...) {}
		LLOG_ERROR << "AddFriend unknown exception" << std::endl;
		return false;
	}


	return true;
}
bool MysqlDao::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& user_info_list) {

	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});


	try {
		// 准备SQL语句, 根据起始id和限制条数返回列表
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("select * from friend where self_id = ? "));

		pstmt->setInt(1, self_id); // 将uid替换为你要查询的uid

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		// 遍历结果集
		while (res->next()) {
			auto friend_id = res->getInt("friend_id");
			auto back = res->getString("back");
			//再一次查询friend_id对应的信息
			auto user_info = GetUser(friend_id);
			if (user_info == nullptr) {
				continue;
			}

			// back 列作为"好友备注"：没有备注时为空，客户端按 备注>昵称>用户名 回退
			user_info->back = back;
			user_info_list.push_back(user_info);
		}
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what();
		LLOG_ERROR << " (MySQL error code: " << e.getErrorCode();
		LLOG_ERROR << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}

	return true;
}
// ===== 聊天记录相关 =====

// 分页拉取用户参与的聊天线程：CTE 把私聊和群聊 UNION 起来，
// WHERE thread_id > lastId 做 keyset 分页，LIMIT pageSize+1 多取一条判断是否还有更多
bool MysqlDao::GetUserThreads(
	int64_t userId,
	int64_t lastId,
	int      pageSize,
	std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
	bool& loadMore,
	int& nextLastId)
{
	// 初始状态
	loadMore = false;
	nextLastId = lastId;
	threads.clear();

	auto con = pool_->getConnection();
	if (!con) {
		return false;
	}
	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});
	auto& conn = con->_con;

	try {
		//群聊分支 join group_chat 带出群名（客户端 1026 建群条目要用）；
		//成员数用 COUNT 子查询（条目显示"群名(N)"）；
		//私聊分支 group_name 给空串、member_count 给 0，两边列数对齐才能 UNION ALL
		std::string sql =
			"WITH all_threads AS ( "
			"  SELECT p.thread_id, 'private' AS type, p.user1_id, p.user2_id, '' AS group_name, 0 AS member_count "
			"    FROM private_chat p "
			"   WHERE (p.user1_id = ? OR p.user2_id = ?) "
			"     AND p.thread_id > ? "
			// 只返回"当前用户仍然保留好友"的私聊线程：
			// 单侧删除好友后，重新登录不能把已删除的会话又拉回来
			"     AND EXISTS (SELECT 1 FROM friend f "
			"                  WHERE f.self_id = ? "
			"                    AND f.friend_id = IF(p.user1_id = ?, p.user2_id, p.user1_id)) "
			"  UNION ALL "
			"  SELECT g.thread_id, 'group' AS type, 0 AS user1_id, 0 AS user2_id, gc.group_name, "
			"    (SELECT COUNT(*) FROM group_chat_member m WHERE m.thread_id = g.thread_id) AS member_count "
			"    FROM group_chat_member g "
			"    JOIN group_chat gc ON gc.thread_id = g.thread_id "
			"   WHERE g.user_id = ? "
			"     AND g.thread_id > ? "
			") "
			"SELECT thread_id, type, user1_id, user2_id, group_name, member_count "
			"  FROM all_threads "
			" ORDER BY thread_id "
			" LIMIT ?;";

		std::unique_ptr<sql::PreparedStatement> pstmt(
			conn->prepareStatement(sql));

		// 绑定参数：
		// userId,userId,lastId（private 基本条件）
		// userId,userId（friend 过滤：self_id + 判断对方是 user1 还是 user2）
		// userId,lastId（group 分支） + LIMIT
		int idx = 1;
		pstmt->setInt64(idx++, userId);              // private.user1_id
		pstmt->setInt64(idx++, userId);              // private.user2_id
		pstmt->setInt64(idx++, lastId);              // private.thread_id > lastId
		pstmt->setInt64(idx++, userId);              // friend.self_id
		pstmt->setInt64(idx++, userId);              // IF(p.user1_id = ?, ...)
		pstmt->setInt64(idx++, userId);              // group.user_id
		pstmt->setInt64(idx++, lastId);              // group.thread_id > lastId
		pstmt->setInt(idx++, pageSize + 1);          // LIMIT pageSize+1

		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

		// 先把所有行读到临时容器
		std::vector<std::shared_ptr<ChatThreadInfo>> tmp;
		while (res->next()) {
			auto cti = std::make_shared<ChatThreadInfo>();
			cti->_thread_id = res->getInt64("thread_id");
			cti->_type = res->getString("type");
			cti->_user1_id = res->getInt64("user1_id");
			cti->_user2_id = res->getInt64("user2_id");
			cti->_group_name = res->getString("group_name");
			cti->_member_count = res->getInt("member_count");
			tmp.push_back(cti);
		}

		// 判断是否多取到一条
		if ((int)tmp.size() > pageSize) {
			loadMore = true;
			tmp.pop_back();  // 丢掉第 pageSize+1 条
		}

		// 如果还有数据，更新 nextLastId 为最后一条的 thread_id
		if (!tmp.empty()) {
			nextLastId = tmp.back()->_thread_id;
		}

		threads = std::move(tmp);
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what()
			<< " (MySQL error code: " << e.getErrorCode()
			<< ", SQLState: " << e.getSQLState() << ")\n";
		return false;
	}

	return true;
}

// 创建（或复用）私聊线程：user1/user2 按 min/max 归一化后靠唯一键去重。
// 事务里先查再插；并发撞唯一键(1062)时回滚后重查一次，拿已有 thread_id
bool MysqlDao::CreatePrivateChat(int user1_id, int user2_id, int& thread_id)
{
	auto con = pool_->getConnection();
	if (!con) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	auto& conn = con->_con;
	int uid1 = std::min(user1_id, user2_id);
	int uid2 = std::max(user1_id, user2_id);
	try {
		// 开启事务
		conn->setAutoCommit(false);
		// 1. 先尝试查询已存在的记录(无锁)
		std::string check_sql =
			"SELECT thread_id FROM private_chat "
			"WHERE user1_id = ? AND user2_id = ?;";

		std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(check_sql));
		pstmt->setInt64(1, uid1);
		pstmt->setInt64(2, uid2);
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

		if (res->next()) {
			// 如果已存在，返回该 thread_id
			thread_id = res->getInt("thread_id");
			conn->commit();  // 提交事务
			return true;
		}

		// 2. 未找到则新建：先插 chat_thread 拿自增 thread_id
		std::string insert_chat_thread_sql =
			"INSERT INTO chat_thread (type, created_at) VALUES ('private', NOW());";

		std::unique_ptr<sql::PreparedStatement> pstmt_insert_thread(conn->prepareStatement(insert_chat_thread_sql));
		pstmt_insert_thread->executeUpdate();

		std::string get_last_insert_id_sql = "SELECT LAST_INSERT_ID();";
		std::unique_ptr<sql::PreparedStatement> pstmt_last_insert_id(conn->prepareStatement(get_last_insert_id_sql));
		std::unique_ptr<sql::ResultSet> res_last_id(pstmt_last_insert_id->executeQuery());
		res_last_id->next();
		thread_id = res_last_id->getInt(1);

		// 3. 在 private_chat 表插入新记录
		std::string insert_private_chat_sql =
			"INSERT INTO private_chat (thread_id, user1_id, user2_id, created_at) "
			"VALUES (?, ?, ?, NOW());";

		std::unique_ptr<sql::PreparedStatement> pstmt_insert_private(conn->prepareStatement(insert_private_chat_sql));
		pstmt_insert_private->setInt64(1, thread_id);
		pstmt_insert_private->setInt64(2, uid1);
		pstmt_insert_private->setInt64(3, uid2);
		pstmt_insert_private->executeUpdate();

		conn->commit();
		return true;
	}
	catch (sql::SQLException& e) {
		conn->rollback();

		// 检查是否是唯一键冲突 (MySQL error code 1062)：并发同时建线程，重查一次
		if (e.getErrorCode() == 1062) {
			try {
				conn->setAutoCommit(true);
				std::string retry_sql =
					"SELECT thread_id FROM private_chat "
					"WHERE user1_id = ? AND user2_id = ?;";
				std::unique_ptr<sql::PreparedStatement> pstmt_retry(
					conn->prepareStatement(retry_sql));
				pstmt_retry->setInt64(1, uid1);
				pstmt_retry->setInt64(2, uid2);
				std::unique_ptr<sql::ResultSet> res_retry(pstmt_retry->executeQuery());

				if (res_retry->next()) {
					thread_id = res_retry->getInt("thread_id");
					return true;
				}
			}
			catch (...) {
				return false;
			}
		}

		LLOG_ERROR << "SQLException: " << e.what()
			<< " (Code: " << e.getErrorCode() << ")" << std::endl;
		return false;
	}
	return false;
}

// ===== day41 群聊 =====
// 建群（事务）：chat_thread(type='group') 拿自增 thread_id → group_chat 存群名/群主
// → group_chat_member 插入全部成员（发起人 + 受邀者）。群不像私聊有"已存在就复用"
// 的唯一键语义，每次调用都新建一个群
bool MysqlDao::CreateGroup(int creator_uid, const std::string& name,
	const std::vector<int>& member_uids, int& thread_id)
{
	auto con = pool_->getConnection();
	if (!con) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	auto& conn = con->_con;
	try {
		conn->setAutoCommit(false);

		// 1. 插 chat_thread 拿自增 thread_id
		std::unique_ptr<sql::PreparedStatement> pstmt_thread(
			conn->prepareStatement("INSERT INTO chat_thread (type, created_at) VALUES ('group', NOW());"));
		pstmt_thread->executeUpdate();

		std::unique_ptr<sql::Statement> stmt_last(conn->createStatement());
		std::unique_ptr<sql::ResultSet> res_last(stmt_last->executeQuery("SELECT LAST_INSERT_ID();"));
		res_last->next();
		thread_id = res_last->getInt(1);

		// 2. 插 group_chat（群名/群主）
		std::unique_ptr<sql::PreparedStatement> pstmt_group(
			conn->prepareStatement("INSERT INTO group_chat (thread_id, group_name, owner_id, created_at) VALUES (?, ?, ?, NOW());"));
		pstmt_group->setInt64(1, thread_id);
		pstmt_group->setString(2, name);
		pstmt_group->setInt64(3, creator_uid);
		pstmt_group->executeUpdate();

		// 3. 插全部群成员（发起人 + 受邀者，成员表复合主键天然去重）
		std::unique_ptr<sql::PreparedStatement> pstmt_member(
			conn->prepareStatement("INSERT INTO group_chat_member (thread_id, user_id, created_at) VALUES (?, ?, NOW());"));
		pstmt_member->setInt64(1, thread_id);
		pstmt_member->setInt64(2, creator_uid);
		pstmt_member->executeUpdate();
		for (auto uid : member_uids) {
			if (uid == creator_uid) {
				continue;   //发起人已插入
			}
			pstmt_member->setInt64(2, uid);
			pstmt_member->executeUpdate();
		}

		conn->commit();
		return true;
	}
	catch (sql::SQLException& e) {
		conn->rollback();
		LLOG_ERROR << "SQLException in CreateGroup: " << e.what()
			<< " (Code: " << e.getErrorCode() << ")" << std::endl;
		return false;
	}
}

// 拉群成员 uid 列表：群消息转发时遍历用（调用方自行排除发送者）
bool MysqlDao::GetGroupMembers(int thread_id, std::vector<int>& member_uids)
{
	auto con = pool_->getConnection();
	if (!con) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		std::unique_ptr<sql::PreparedStatement> pstmt(
			con->_con->prepareStatement("SELECT user_id FROM group_chat_member WHERE thread_id = ?"));
		pstmt->setInt64(1, thread_id);
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		while (res->next()) {
			member_uids.push_back(res->getInt("user_id"));
		}
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException in GetGroupMembers: " << e.what() << std::endl;
		return false;
	}
}

// 按 thread_id 分页拉历史消息：message_id > lastId 正序翻页，多取一条判断 load_more
std::shared_ptr<PageResult> MysqlDao::LoadChatMsg(int thread_id, int last_message_id,
	int page_size, int uid)
{
	auto con = pool_->getConnection();
	if (!con) {
		return nullptr;
	}
	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});
	auto& conn = con->_con;

	try {
		auto page_res = std::make_shared<PageResult>();
		page_res->load_more = false;
		// 排除当前用户删除过的消息：删除只影响自己视角，
		// 对方的历史记录/未读状态不受影响（类似微信"删除"而不是"撤回"）
		const std::string sql = R"(
        SELECT cm.message_id, cm.thread_id, cm.sender_id, cm.recv_id, cm.content,
               cm.created_at, cm.updated_at, cm.status, cm.msg_type
        FROM chat_message cm
        WHERE cm.thread_id = ?
          AND cm.message_id > ?
          AND NOT EXISTS (
              SELECT 1 FROM chat_message_deleted d
              WHERE d.message_id = cm.message_id AND d.uid = ?
          )
        ORDER BY cm.message_id ASC
        LIMIT ?
		)";

		uint32_t fetch_limit = page_size + 1;
		auto pstmt = std::unique_ptr<sql::PreparedStatement>(
			conn->prepareStatement(sql)
			);
		pstmt->setInt(1, thread_id);
		pstmt->setInt(2, last_message_id);
		pstmt->setInt(3, uid);
		pstmt->setInt(4, fetch_limit);

		auto rs = std::unique_ptr<sql::ResultSet>(pstmt->executeQuery());

		while (rs->next()) {
			ChatMessage msg;
			msg.message_id = rs->getUInt64("message_id");
			msg.thread_id = rs->getUInt64("thread_id");
			msg.sender_id = rs->getUInt64("sender_id");
			msg.recv_id = rs->getUInt64("recv_id");
			msg.content = rs->getString("content");
			msg.chat_time = rs->getString("created_at");
			msg.status = rs->getInt("status");
			msg.msg_type = rs->getInt("msg_type");
			page_res->messages.push_back(std::move(msg));
		}
		if (page_res->messages.size() > page_size) {
			page_res->messages.pop_back();
			page_res->load_more = true;
		}

		//空页时 last_message_id 就是 next_cursor
		if (!page_res->messages.empty()) {
			page_res->next_cursor = page_res->messages.back().message_id;
		}
		else {
			page_res->next_cursor = last_message_id;
		}

		return page_res;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what() << std::endl;
		return nullptr;
	}
	return nullptr;
}

// 删除单条消息（仅当前用户视角）：写一条删除标记。
// 用 INSERT IGNORE 保证重复删除幂等；历史加载时通过 NOT EXISTS 过滤掉
bool MysqlDao::DeleteChatMsgForUser(int message_id, int uid)
{
	auto con = pool_->getConnection();
	if (!con) {
		return false;
	}
	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});
	auto& conn = con->_con;

	try {
		const std::string sql =
			"INSERT IGNORE INTO chat_message_deleted(message_id, uid) VALUES(?, ?)";
		std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
		pstmt->setInt64(1, message_id);
		pstmt->setInt(2, uid);
		pstmt->executeUpdate();
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException in DeleteChatMsgForUser: " << e.what() << std::endl;
		return false;
	}
}

// 修改个人资料：昵称 + 描述。desc 是 MySQL 关键字，必须用反引号包起来
bool MysqlDao::UpdateUserInfo(int uid, const std::string& nick, const std::string& desc)
{
	auto con = pool_->getConnection();
	if (!con) {
		return false;
	}
	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});
	auto& conn = con->_con;

	try {
		const std::string sql = "UPDATE user SET nick = ?, `desc` = ? WHERE uid = ?";
		std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
		pstmt->setString(1, nick);
		pstmt->setString(2, desc);
		pstmt->setInt(3, uid);
		pstmt->executeUpdate();
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException in UpdateUserInfo: " << e.what() << std::endl;
		return false;
	}
}

// 好友备注：只更新 self_id → friend_id 这一行的 back 列，
// 对方看到的名字不受影响（单侧备注）
bool MysqlDao::UpdateFriendRemark(int self_id, int friend_id, const std::string& remark)
{
	auto con = pool_->getConnection();
	if (!con) {
		return false;
	}
	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});
	auto& conn = con->_con;

	try {
		const std::string sql = "UPDATE friend SET back = ? WHERE self_id = ? AND friend_id = ?";
		std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
		pstmt->setString(1, remark);
		pstmt->setInt(2, self_id);
		pstmt->setInt(3, friend_id);
		pstmt->executeUpdate();
		// 备注和原值相同 affected=0，也算成功；只有 SQL 异常才算失败
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException in UpdateFriendRemark: " << e.what() << std::endl;
		return false;
	}
}

// 删除好友：只删自己这一侧的 friend 行（对方好友列表保留）
bool MysqlDao::DeleteFriendOneSide(int self_id, int friend_id)
{
	auto con = pool_->getConnection();
	if (!con) {
		return false;
	}
	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});
	auto& conn = con->_con;

	try {
		const std::string sql = "DELETE FROM friend WHERE self_id = ? AND friend_id = ?";
		std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
		pstmt->setInt(1, self_id);
		pstmt->setInt(2, friend_id);
		pstmt->executeUpdate();
		// 幂等：重复删除/好友本来就不存在也当成功，客户端可以正常清理残留会话
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException in DeleteFriendOneSide: " << e.what() << std::endl;
		return false;
	}
}

// 批量插入聊天消息，插入后用 LAST_INSERT_ID() 回填自增 message_id（事务保证原子性）
bool MysqlDao::AddChatMsg(std::vector<std::shared_ptr<ChatMessage>>& chat_datas) {
	auto con = pool_->getConnection();
	if (!con) {
		return false;
	}
	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});
	auto& conn = con->_con;

	try {
		//关闭自动提交，以手动管理事务
		conn->setAutoCommit(false);
		auto pstmt = std::unique_ptr<sql::PreparedStatement>(
			conn->prepareStatement(
				"INSERT INTO chat_message "
				"(thread_id, sender_id, recv_id, content, created_at, updated_at, status, msg_type) "
				"VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
			)
		);

		for (auto& msg : chat_datas) {
			pstmt->setUInt64(1, msg->thread_id);
			pstmt->setUInt64(2, msg->sender_id);
			pstmt->setUInt64(3, msg->recv_id);
			pstmt->setString(4, msg->content);
			pstmt->setString(5, msg->chat_time);  // created_at
			pstmt->setString(6, msg->chat_time);  // updated_at
			pstmt->setInt(7, msg->status);
			pstmt->setInt(8, msg->msg_type);
			pstmt->executeUpdate();

			// 取回自增主键，回填给调用方（回执要用）
			std::unique_ptr<sql::Statement> keyStmt(
				conn->createStatement()
			);
			std::unique_ptr<sql::ResultSet> rs(
				keyStmt->executeQuery("SELECT LAST_INSERT_ID()")
			);
			if (rs->next()) {
				msg->message_id = rs->getUInt64(1);
			}
			else {
				continue;
			}
		}

		conn->commit();
		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException: " << e.what() << std::endl;
		conn->rollback();
		return false;
	}
	return true;
}
