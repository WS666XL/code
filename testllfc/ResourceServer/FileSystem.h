#pragma once
#include "const.h"
#include "Singleton.h"
#include "data.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <functional>

class CSession;

//文件写入线程：同一个线程只处理哈希到它的那些文件，
//同一文件名的所有分块都会落到同一线程，天然保证写盘顺序
//（这也是 1031 头像"最后一块才更新 DB/Redis"能安全的根基：
//  最后一块必然在前面所有块处理完之后才被处理到）
class FileWorker
{
public:
	FileWorker();
	~FileWorker();
	void PostTask(std::shared_ptr<FileTask> task);
private:
	void task_callback(std::shared_ptr<FileTask> task);
	void RegisterHandlers();
	std::thread _work_thread;
	std::queue<std::shared_ptr<FileTask>> _task_que;
	std::atomic<bool> _b_stop;
	std::mutex  _mtx;
	std::condition_variable _cv;
	//消息 ID → 落盘处理函数：1003 纯落盘，1031 落盘+更新 MySQL/Redis
	std::unordered_map<short, std::function<void(std::shared_ptr<FileTask>)>> _handlers;
};

//文件系统单例：持有 FILE_WORKER_COUNT 个 FileWorker，
//按文件名哈希把任务分发下去（同名文件串行写，不同文件并行写）
class FileSystem :public Singleton<FileSystem>
{
	friend class Singleton<FileSystem>;
public:
	~FileSystem();
	void PostMsgToQue(shared_ptr<FileTask> msg, int index);
private:
	FileSystem();
	std::vector<std::shared_ptr<FileWorker>>  _file_workers;
};
