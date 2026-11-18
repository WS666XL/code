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

	// ===== 文件秒传索引（MySQL 为最终真相，Redis 只是缓存） =====
	bool GetFileObject(const std::string& md5, FileObjectInfo& info) {
		return _dao.GetFileObject(md5, info);
	}
	bool UpsertFileObject(const std::string& md5, int64_t file_size,
		const std::string& rel_path) {
		return _dao.UpsertFileObject(md5, file_size, rel_path);
	}
	bool DeleteFileObject(const std::string& md5) {
		return _dao.DeleteFileObject(md5);
	}
private:
	MysqlMgr() {
	}
	MysqlDao _dao;
};
