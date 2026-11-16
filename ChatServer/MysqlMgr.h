#pragma once
#include "const.h"
#include "MysqlDao.h"
#include "Singleton.h"
class MysqlMgr: public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr();
	int RegUser(const std::string& name, const std::string& email,  const std::string& pwd);
	bool CheckEmail(const std::string& name, const std::string & email);
	bool UpdatePwd(const std::string& name, const std::string& email);
	bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
	bool AddFriendApply(const int& from, const int& to);
	bool GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit = 10);
	bool AuthFriendApply(const int& from, const int& to);
	bool AddFriend(const int& from, const int& to, std::string back_name);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& user_info);
	std::shared_ptr<UserInfo> GetUser(int uid);
	std::shared_ptr<UserInfo> GetUser(std::string name);

	bool GetUserThreads(
		int64_t userId,
		int64_t lastId,
		int      pageSize,
		std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
		bool& loadMore,
		int& nextLastId);
	bool CreatePrivateChat(int user1_id, int user2_id, int& thread_id);
	// day41 群聊
	bool CreateGroup(int creator_uid, const std::string& name,
		const std::vector<int>& member_uids, int& thread_id) {
		return _dao.CreateGroup(creator_uid, name, member_uids, thread_id);
	}
	bool GetGroupMembers(int thread_id, std::vector<int>& member_uids) {
		return _dao.GetGroupMembers(thread_id, member_uids);
	}
	std::shared_ptr<PageResult> LoadChatMsg(int threadId, int lastId, int pageSize, int uid);
	bool AddChatMsg(std::vector<std::shared_ptr<ChatMessage>>& chat_datas);
	bool DeleteChatMsgForUser(int messageId, int uid) {
		return _dao.DeleteChatMsgForUser(messageId, uid);
	}
private:
	MysqlMgr();
	MysqlDao  _dao;
};

