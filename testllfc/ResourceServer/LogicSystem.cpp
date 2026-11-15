#include "LogicSystem.h"
#include "FileSystem.h"
#include "RedisMgr.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

LogicWorker::LogicWorker() :_b_stop(false)
{
	RegisterCallBacks();

	_work_thread = std::thread([this]() {
		while (!_b_stop) {
			std::unique_lock<std::mutex> lock(_mtx);
			_cv.wait(lock, [this]() {
				if (_b_stop) {
					return true;
				}

				if (_task_que.empty()) {
					return false;
				}

				return true;

			});

			if (_b_stop) {
				return;
			}

			auto task = _task_que.front();
			task_callback(task);
			_task_que.pop();
		}

	});
}

LogicWorker::~LogicWorker()
{
	_b_stop = true;
	_cv.notify_one();
	_work_thread.join();
}

void LogicWorker::PostTask(std::shared_ptr<LogicNode> task)
{
	{
		std::lock_guard<std::mutex> lock(_mtx);
		_task_que.push(task);
	}

	_cv.notify_one();
}

//按消息 id 查回调表并执行；没注册的 id 直接丢弃并打日志
void LogicWorker::task_callback(std::shared_ptr<LogicNode> task)
{
	cout << "recv_msg id  is " << task->_recvnode->_msg_id << endl;
	auto call_back_iter = _fun_callbacks.find(task->_recvnode->_msg_id);
	if (call_back_iter == _fun_callbacks.end()) {
		std::cout << "msg id [" << task->_recvnode->_msg_id << "] handler not found" << std::endl;
		return;
	}
	call_back_iter->second(task->_session, task->_recvnode->_msg_id,
		std::string(task->_recvnode->_data, task->_recvnode->_cur_len));
}

void LogicWorker::RegisterCallBacks()
{
	//测试消息：原样回显，用于链路自测（客户端 FileTcpMgr 连上后先发这个验证通不通）
	_fun_callbacks[ID_TEST_MSG_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);
			auto data = root["data"].asString();
			std::cout << "recv test data is  " << data << std::endl;

			Json::Value  rtvalue;
			rtvalue["error"] = ErrorCodes::Success;
			rtvalue["data"] = data;
			//Defer 放最后：保证 rtvalue 组装完毕后再序列化发送
			Defer defer([this, &rtvalue, session]() {
				std::string return_str = rtvalue.toStyledString();
				session->Send(return_str, ID_TEST_MSG_RSP);
				});
	};

	//上传文件分块：解析字段 → 投递到 FileSystem（按文件名哈希选线程）→ 立即回执进度
	//真正的落盘在 FileWorker 线程里异步完成，逻辑线程只做转发，不阻塞其他连接
	_fun_callbacks[ID_UPLOAD_FILE_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);
			auto seq = root["seq"].asInt();
			auto name = root["name"].asString();
			auto total_size = root["total_size"].asInt();
			auto trans_size = root["trans_size"].asInt();
			auto last = root["last"].asInt();
			auto file_data = root["data"].asString();
			Json::Value  rtvalue;
			Defer defer([this, &rtvalue, session]() {
				std::string return_str = rtvalue.toStyledString();
				session->Send(return_str, ID_UPLOAD_FILE_RSP);
				});

			// 按 std::hash(文件名) % FILE_WORKER_COUNT 选择文件线程：
			// 同名文件的所有分块都在同一线程串行写盘，保证顺序
			std::hash<std::string> hash_fn;
			size_t hash_value = hash_fn(name);
			int index = hash_value % FILE_WORKER_COUNT;
			std::cout << "Hash value: " << hash_value << std::endl;

			FileSystem::GetInstance()->PostMsgToQue(
				std::make_shared<FileTask>(session, ID_UPLOAD_FILE_REQ, name, seq, total_size,
					trans_size, last, file_data, 0),
				index
			);

			rtvalue["error"] = ErrorCodes::Success;
			rtvalue["total_size"] = total_size;
			rtvalue["seq"] = seq;
			rtvalue["name"] = name;
			rtvalue["trans_size"] = trans_size;
			rtvalue["last"] = last;
	};

	// 1031 上传头像分块：与 1003 的差别——
	// 1. 请求里多带 uid/token/last_seq；首块时用 Redis 里的 utoken_<uid> 校验 token，
	//    防止未登录的人往别人目录里塞文件（老师版 day39 同款校验）
	// 2. 回包时机不同：1003 解析完立即回 1004（不等落盘），1031 要等 FileWorker
	//    落盘（最后一块还要更新 DB/Redis）后才回 1032，所以回包逻辑包在 callback 里
	// 3. 老师版首块还会往 Redis 写 FileInfo 做断点续传（day38 内容），
	//    本版没做断点续传，跳过该步，只保留 token 校验
	_fun_callbacks[ID_UPLOAD_HEAD_ICON_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);
			auto md5 = root["md5"].asString();
			auto seq = root["seq"].asInt();
			auto name = root["name"].asString();
			auto total_size = root["total_size"].asInt();
			auto trans_size = root["trans_size"].asInt();
			auto last = root["last"].asInt();
			auto file_data = root["data"].asString();
			auto uid = root["uid"].asInt();
			auto token = root["token"].asString();
			auto last_seq = root["last_seq"].asInt();

			//callback 在 FileWorker 线程里被调用（落盘完成后）：
			//把回执字段补齐后用 session->Send 回 1032（Send 内部有锁，跨线程安全）
			auto callback = [session, total_size, seq, name, trans_size, last,
				md5, uid, last_seq](const Json::Value& result) {
					Json::Value rtvalue = result;
					rtvalue["total_size"] = total_size;
					rtvalue["seq"] = seq;
					rtvalue["name"] = name;
					rtvalue["trans_size"] = trans_size;
					rtvalue["last"] = last;
					rtvalue["md5"] = md5;
					rtvalue["uid"] = uid;
					rtvalue["last_seq"] = last_seq;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
			};

			//首块校验 token：从 Redis 取该 uid 登录时写入的令牌比对；
			//校验失败的直接回 1032 带 error，不投递落盘任务
			if (seq == 1) {
				std::string uid_str = std::to_string(uid);
				std::string token_key = USERTOKENPREFIX + uid_str;
				std::string token_value = "";
				bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
				if (!success) {
					Json::Value rtvalue;
					rtvalue["error"] = ErrorCodes::UidInvalid;
					rtvalue["md5"] = md5;
					rtvalue["seq"] = seq;
					rtvalue["name"] = name;
					rtvalue["uid"] = uid;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
					return;
				}

				if (token_value != token) {
					Json::Value rtvalue;
					rtvalue["error"] = ErrorCodes::TokenInvalid;
					rtvalue["md5"] = md5;
					rtvalue["seq"] = seq;
					rtvalue["name"] = name;
					rtvalue["uid"] = uid;
					std::string return_str = rtvalue.toStyledString();
					session->Send(return_str, ID_UPLOAD_HEAD_ICON_RSP);
					return;
				}
			}

			//与 1003 相同：按文件名哈希选 FileWorker，
			//同一头像（同名文件）的所有分块落到同一线程串行落盘
			std::hash<std::string> hash_fn;
			size_t hash_value = hash_fn(name);
			int index = hash_value % FILE_WORKER_COUNT;

			auto task = std::make_shared<FileTask>(session, ID_UPLOAD_HEAD_ICON_REQ, name, seq,
				total_size, trans_size, last, file_data, uid);
			task->_callback = callback;
			FileSystem::GetInstance()->PostMsgToQue(task, index);
	};
}

LogicSystem::LogicSystem(){
	for (int i = 0; i < LOGIC_WORKER_COUNT; i++) {
		_workers.push_back(std::make_shared<LogicWorker>());
	}
}

LogicSystem::~LogicSystem(){

}

void LogicSystem::PostMsgToQue(shared_ptr<LogicNode> msg, int index) {
	_workers[index]->PostTask(msg);
}
