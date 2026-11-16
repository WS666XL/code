#include "LogicSystem.h"
#include "FileSystem.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "base64.h"
#include <fstream>
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
			// 临时调试：定位分块重组丢块问题（data 字段解码前的原始大小）
			std::cout << "1031 parse: seq=" << seq << " data_len=" << file_data.size()
				<< " total=" << total_size << " last=" << last << std::endl;

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
					// 临时调试：打印校验失败原因（客户端 token 与 Redis 不一致时静默丢弃很难排查）
					std::cout << "1031 token invalid: no token in redis, uid=" << uid_str << std::endl;
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
					// 临时调试：打印双方 token 前缀（完整值不落日志）
					std::cout << "1031 token mismatch: got [" << token.substr(0, 8)
						<< "...] expect [" << token_value.substr(0, 8) << "...]" << std::endl;
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
			// 临时调试：投递时打印 seq 与目标 worker，对照 worker 执行日志
			std::cout << "1031 dispatch: seq=" << seq << " index=" << index
				<< " name=[" << name << "]" << std::endl;

			auto task = std::make_shared<FileTask>(session, ID_UPLOAD_HEAD_ICON_REQ, name, seq,
				total_size, trans_size, last, file_data, uid);
			task->_callback = callback;
			FileSystem::GetInstance()->PostMsgToQue(task, index);
	};

	// 1033 下载文件（头像回显链路）：客户端本地没有对方的头像文件时发起。
	// 请求 {name(文件名), seq, token, uid(请求者), owner_uid(文件所有者), client_path(客户端保存路径)}
	// 回包 {error, data(base64分块), seq, total_size, current_size, is_last, name, client_path}
	// 老师版把下载放 FileWorker 是为了配合 day38 断点续传（Redis 存 FileInfo 定位路径），
	// 本版精简：请求直接带 owner_uid 拼路径，读文件是很快的同步 IO，LogicWorker 直接处理即可
	_fun_callbacks[ID_DOWN_LOAD_FILE_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);
			auto name = root["name"].asString();
			auto seq = root["seq"].asInt();
			auto token = root["token"].asString();
			auto uid = root["uid"].asInt();
			auto owner_uid = root["owner_uid"].asInt();
			auto client_path = root["client_path"].asString();

			//回包公共字段用 Defer 兜底发送（handler 中途 return 也不会漏发）
			Json::Value rtvalue;
			rtvalue["name"] = name;
			rtvalue["client_path"] = client_path;
			rtvalue["seq"] = seq;
			Defer defer([this, &rtvalue, session]() {
				std::string return_str = rtvalue.toStyledString();
				session->Send(return_str, ID_DOWN_LOAD_FILE_RSP);
				});

			//登录态校验：防止任何人凭文件名扫描下载他人文件
			if (seq == 1) {
				std::string token_key = USERTOKENPREFIX + std::to_string(uid);
				std::string token_value = "";
				bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
				if (!success) {
					rtvalue["error"] = ErrorCodes::UidInvalid;
					return;
				}
				if (token_value != token) {
					rtvalue["error"] = ErrorCodes::TokenInvalid;
					return;
				}
			}

			//文件名安全校验：name 只应是纯文件名，带路径分隔符直接拒绝，
			//防止 "../" 之类的目录穿越读任意文件
			if (name.empty() || name.find('/') != std::string::npos
				|| name.find("\\") != std::string::npos
				|| name.find("..") != std::string::npos) {
				rtvalue["error"] = ErrorCodes::FileNotExists;
				return;
			}

			auto file_path = ConfigMgr::Inst().GetFileOutPath();
			auto file_path_str = (file_path / std::to_string(owner_uid) / name).string();

			std::ifstream infile(file_path_str, std::ios::binary);
			if (!infile) {
				std::cerr << "download file not exists: " << file_path_str << std::endl;
				rtvalue["error"] = ErrorCodes::FileNotExists;
				return;
			}

			//定位到 (seq-1)*FILE_CHUNK_LEN 偏移读一块——与上传的 32KB 分块对称
			infile.seekg(0, std::ios::end);
			std::streamsize total_size = infile.tellg();
			std::streamsize offset = (std::streamsize)(seq - 1) * FILE_CHUNK_LEN;
			if (offset >= total_size) {
				rtvalue["error"] = ErrorCodes::FileNotExists;
				return;
			}
			infile.seekg(offset);

			char buffer[FILE_CHUNK_LEN];
			infile.read(buffer, FILE_CHUNK_LEN);
			std::streamsize bytes_read = infile.gcount();
			infile.close();

			if (bytes_read <= 0) {
				rtvalue["error"] = ErrorCodes::FileReadFailed;
				return;
			}

			//读出的分块 base64 编码后回给客户端
			std::string data_to_encode(buffer, bytes_read);
			rtvalue["data"] = base64_encode(data_to_encode);
			std::streamsize current_pos = offset + bytes_read;
			rtvalue["total_size"] = std::to_string(total_size);
			rtvalue["current_size"] = std::to_string(current_pos);
			rtvalue["is_last"] = (current_pos >= total_size);
			rtvalue["error"] = ErrorCodes::Success;
	};

	// 1037 上传聊天图片分块：客户端在 1035（图片消息入库拿到 message_id）之后
	// 才开始传文件，请求字段比 1003 多 md5/message_id/sender/receiver。
	// 时序（老师版 day39-40 设计）：文件全部落盘后才由 ResourceServer 通过 gRPC
	// 通知对方所在 ChatServer 推 1039，保证对方收到通知时图片已可下载
	_fun_callbacks[ID_IMG_CHAT_UPLOAD_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
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
			auto message_id = root["message_id"].asInt();
			auto sender = root["sender"].asInt();
			auto receiver = root["receiver"].asInt();

			//与 1003 相同：解析完立即回 1038 进度回包，落盘在 FileWorker 异步完成
			Json::Value  rtvalue;
			Defer defer([this, &rtvalue, session]() {
				std::string return_str = rtvalue.toStyledString();
				session->Send(return_str, ID_IMG_CHAT_UPLOAD_RSP);
				});

			//首块校验登录态（与 1031 同款校验）
			if (seq == 1) {
				std::string token_key = USERTOKENPREFIX + std::to_string(uid);
				std::string token_value = "";
				bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
				if (!success) {
					rtvalue["error"] = ErrorCodes::UidInvalid;
					return;
				}
				if (token_value != token) {
					rtvalue["error"] = ErrorCodes::TokenInvalid;
					return;
				}
			}

			//按文件名哈希选 FileWorker：同一图片的所有分块串行落盘（保证顺序）
			std::hash<std::string> hash_fn;
			size_t hash_value = hash_fn(name);
			int index = hash_value % FILE_WORKER_COUNT;

			auto task = std::make_shared<FileTask>(session, ID_IMG_CHAT_UPLOAD_REQ, name, seq,
				total_size, trans_size, last, file_data, sender);
			//1037 专属：消息 id（落盘完更新状态用）+ 收图人（查节点推通知用）
			task->_chat_msg_id = message_id;
			task->_receiver = receiver;
			FileSystem::GetInstance()->PostMsgToQue(task, index);

			rtvalue["error"] = ErrorCodes::Success;
			rtvalue["total_size"] = total_size;
			rtvalue["seq"] = seq;
			rtvalue["name"] = name;
			rtvalue["trans_size"] = trans_size;
			rtvalue["last"] = last;
			rtvalue["message_id"] = message_id;
			rtvalue["sender"] = sender;
			rtvalue["receiver"] = receiver;
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
