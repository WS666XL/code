#pragma once
#include "Singleton.h"
#include "const.h"
#include "data.h"
#include "MysqlDao.h"
#include <memory>

//MySQL 管理单例：包一层 DAO，供 FileWorker 的 1031 处理调用
//（落盘完成后更新 user.icon + 读整行写 Redis 缓存）
class MysqlMgr : public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr() {
	}
	bool UpdateUserIcon(int uid, const std::string& icon) {
		return _dao.UpdateHeadInfo(uid, icon);
	}
	std::shared_ptr<UserInfo> GetUser(int uid) {
		return _dao.GetUser(uid);
	}
	std::shared_ptr<ChatMessage> GetChatMsgById(int message_id) {
		return _dao.GetChatMsgById(message_id);
	}
	bool UpdateUploadStatus(int chat_message_id) {
		return _dao.UpdateUploadStatus(chat_message_id);
	}
private:
	MysqlMgr() {
	}
	MysqlDao _dao;
};
