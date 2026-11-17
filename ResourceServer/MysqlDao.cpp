#include "MysqlDao.h"
#include "Logger.h"
#include "ConfigMgr.h"

MysqlDao::MysqlDao()
{
	auto& cfg = ConfigMgr::Inst();
	const auto& host = cfg["Mysql"]["Host"];
	const auto& port = cfg["Mysql"]["Port"];
	const auto& pwd = cfg["Mysql"]["Passwd"];
	const auto& schema = cfg["Mysql"]["Schema"];
	const auto& user = cfg["Mysql"]["User"];
	pool_.reset(new MySqlPool(host + ":" + port, user, pwd, schema, 5));
}

MysqlDao::~MysqlDao() {
	pool_->Close();
}

//按 uid 读 user 表整行：头像收尾时要拿最新数据（含刚更新的 icon）
//整行写回 Redis 缓存，字段与 ChatServer 的 GetBaseInfo 写缓存保持一致，
//这样两边写的缓存格式相同，谁先写谁后写都互相兼容
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
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE uid = ?"));
		pstmt->setInt(1, uid);

		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<UserInfo> user_ptr = nullptr;
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

//更新头像字段：icon 存的是服务器落盘的文件名（如 "head.png"），不是完整路径。
//客户端拿到文件名后自己拼下载 URL / 读本地缓存
bool MysqlDao::UpdateHeadInfo(int uid, const std::string& icon)
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
		std::string update_sql =
			"UPDATE user SET icon = ? WHERE uid = ?;";

		std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(update_sql));
		pstmt->setString(1, icon);
		pstmt->setInt64(2, uid);

		int affected_rows = pstmt->executeUpdate();
		// 检查是否找到并更新了记录
		if (affected_rows == 0) {
			LLOG_ERROR << "No user found with uid: " << uid << std::endl;
			return false;
		}

		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException in UpdateHeadInfo: " << e.what() << std::endl;
		return false;
	}
}

//按 message_id 读图片消息详情：图片文件传完后推 gRPC 通知时，
//需要拿发件人/收件人/唯一文件名（content）重新组 NotifyChatImgReq
std::shared_ptr<ChatMessage> MysqlDao::GetChatMsgById(int message_id)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		std::unique_ptr<sql::PreparedStatement> pstmt(
			con->_con->prepareStatement("SELECT * FROM chat_message WHERE message_id = ?"));
		pstmt->setInt(1, message_id);

		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<ChatMessage> msg_ptr = nullptr;
		while (res->next()) {
			msg_ptr.reset(new ChatMessage);
			msg_ptr->message_id = message_id;
			msg_ptr->sender_id = res->getInt("sender_id");
			msg_ptr->recv_id = res->getInt("recv_id");
			msg_ptr->thread_id = res->getInt("thread_id");
			msg_ptr->content = res->getString("content");
			break;
		}
		return msg_ptr;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException in GetChatMsgById: " << e.what() << std::endl;
		return nullptr;
	}
}

//图片消息上传状态：图片先入库（status=0 表示资源未上传），
//最后一块文件落盘成功后置 2（READED），与文本消息的最终状态对齐
bool MysqlDao::UpdateUploadStatus(int chat_message_id)
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
		std::string update_sql =
			"UPDATE chat_message SET status = ? WHERE message_id = ?;";

		std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(update_sql));
		pstmt->setInt(1, 2);   // MsgStatus::READED
		pstmt->setInt64(2, chat_message_id);

		int affected_rows = pstmt->executeUpdate();
		if (affected_rows == 0) {
			LLOG_ERROR << "No chat message found with message_id: " << chat_message_id << std::endl;
			return false;
		}

		return true;
	}
	catch (sql::SQLException& e) {
		LLOG_ERROR << "SQLException in UpdateUploadStatus: " << e.what() << std::endl;
		return false;
	}
}
