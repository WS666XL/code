#pragma once
#include"Singleton.h"
#include<memory>
#include<mutex>
#include<unordered_map>
#include<vector>
class CSession;
class UserMgr :public Singleton<UserMgr>
{
	friend class Singleton<UserMgr>;
public:
	~UserMgr();
	std::shared_ptr<CSession> GetSession(int uid);
	void SetUserSession(int uid, std::shared_ptr<CSession> session);
	void RmvUserSession(int uid);
	//本节点全部在线 session 的快照（锁内拷贝）：gRPC 线程遍历推送用，
	//不返回引用——遍历期间若有人登录/登出改 map 会悬空，拷贝出去再慢慢 Send
	std::vector<std::shared_ptr<CSession>> GetOnlineSessions();
private:
	UserMgr();
	std::mutex _session_mtx;
	std::unordered_map<int, std::shared_ptr<CSession>> _uid_to_session;
};

