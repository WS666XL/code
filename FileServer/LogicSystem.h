#pragma once
#include "Singleton.h"
#include "const.h"
#include "data.h"
#include "CSession.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <map>

//一个逻辑线程：独立线程消费自己的任务队列，
//同一 session 的消息固定进同一个 worker（CSession 里按 session_id 哈希），保证有序
class LogicWorker
{
public:
	LogicWorker();
	~LogicWorker();
	void PostTask(std::shared_ptr<LogicNode> task);
	void RegisterCallBacks();
private:
	void task_callback(std::shared_ptr<LogicNode> task);
	std::thread _work_thread;
	std::queue<std::shared_ptr<LogicNode>> _task_que;
	std::atomic<bool> _b_stop;
	std::mutex  _mtx;
	std::condition_variable _cv;
	std::unordered_map<short, std::function<void(std::shared_ptr<CSession>, const short& msg_id, const std::string& msg_data)>> _fun_callbacks;
};

//逻辑系统单例：持有 LOGIC_WORKER_COUNT 个 LogicWorker，
//1001/1003 等消息的处理逻辑都注册在 worker 的回调表里
class LogicSystem :public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem();
	void PostMsgToQue(shared_ptr<LogicNode> msg, int index);
private:
	LogicSystem();
	std::vector<std::shared_ptr<LogicWorker> > _workers;
};
