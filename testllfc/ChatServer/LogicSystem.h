#pragma once
#include "Singleton.h"
#include <queue>
#include <thread>
#include "CSession.h"
#include <queue>
#include <map>
#include <functional>
#include "const.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <unordered_map>
#include "data.h"
#include<mutex>
//后期可以增加多个处理不同逻辑的队列，现在是一个线程来处理一个队列
typedef  function<void(shared_ptr<CSession>, const short &msg_id, const string &msg_data)> FunCallBack;
class LogicSystem:public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem();
	void PostMsgToQue(shared_ptr < LogicNode> msg);
	// 注册 CServer 指针：踢人时需要通过它清除旧 session
	void SetServer(std::shared_ptr<CServer> pserver);
private:
	LogicSystem();
	void DealMsg();
	void RegisterCallBacks();
	void LoginHandler(shared_ptr<CSession>session, const short &msg_id, const string &msg_data);
	void SearchInfo(shared_ptr<CSession>session, const short& msg_id, const string& msg_data);
	void AddFriendApply(shared_ptr<CSession>session, const short& msg_id, const string& msg_data);
	void AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data);
	void DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data);
	// 心跳处理：客户端每隔 10s 发一次，回 1024 让客户端知道链路还活着
	void HeartBeatHandler(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data);
	// 加载聊天线程列表（1025→1026）：登录后拉取用户参与的所有会话
	void GetUserThreadsHandler(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data);
	// 查询聊天线程的内部封装，直接转给 MysqlMgr
	bool GetUserThreads(int64_t userId,
		int64_t lastId,
		int      pageSize,
		std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
		bool& loadMore,
		int& nextLastId);
	// 创建私聊线程（1027→1028）：已存在则复用，不存在才新建
	void CreatePrivateChat(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data);
	// 分页加载某个线程的历史消息（1029→1030）
	void LoadChatMsg(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data);
	void DealChatImgMsg(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data);
	// day41 群聊：建群（发起人选好友成群）
	void DealCreateGroup(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data);
	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);
	bool isPureDigit(const std::string& str);
	void GetUserByUid(std::string uid_str, Json::Value& rtvalue);
	void GetUserByName(std::string name, Json::Value& rtvalue);
	bool GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& list);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_list);
	
	std::thread _worker_thread;
	std::queue<shared_ptr<LogicNode>> _msg_que;
	std::mutex _mutex;
	std::condition_variable _consume;
	bool _b_stop;
	std::map<short, FunCallBack> _fun_callbacks;
	std::unordered_map<int, std::shared_ptr<UserInfo>> _users;
	std::shared_ptr<CServer> _p_server; // 所属 CServer（main 里通过 SetServer 注入）
};

