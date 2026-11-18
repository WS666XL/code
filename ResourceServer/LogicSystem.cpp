#include "LogicSystem.h"
#include "Logger.h"
#include "FileSystem.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "MysqlMgr.h"
#include "base64.h"
#include <fstream>
#include <boost/filesystem.hpp>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

namespace {
	//上传进度 key：uid|md5。带 uid 是为了隔离不同用户上传同一份文件（md5 相同）时的进度；
	//1003 通用上传没有身份字段，uid=0 即可
	std::string UploadProgressKey(int uid, const std::string& md5) {
		return std::to_string(uid) + "|" + md5;
	}

	//下载进度 key：owner_uid|name。不同用户的头像都叫 head.png，
	//只按文件名存 Redis 会互相覆盖，必须带上所有者 uid
	std::string DownloadProgressKey(int owner_uid, const std::string& name) {
		return std::to_string(owner_uid) + "|" + name;
	}
}

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
	LLOG_INFO << "recv_msg id  is " << task->_recvnode->_msg_id << endl;
	auto call_back_iter = _fun_callbacks.find(task->_recvnode->_msg_id);
	if (call_back_iter == _fun_callbacks.end()) {
		LLOG_INFO << "msg id [" << task->_recvnode->_msg_id << "] handler not found" << std::endl;
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
			LLOG_INFO << "recv test data is  " << data << std::endl;

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
			auto md5 = root["md5"].asString();
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
			// 先填公共字段：中途 return 的错误回包也要带 md5/name/seq，
			// 客户端才能找回上传上下文并自动从第 1 块重传
			rtvalue["md5"] = md5;
			rtvalue["name"] = name;
			rtvalue["seq"] = seq;
			rtvalue["total_size"] = total_size;
			rtvalue["trans_size"] = trans_size;
			rtvalue["last"] = last;

			// ===== day38 断点续传：每块都把进度写进 Redis =====
			// 客户端暂停/断线后再发 1005，就能从这里读到"传到第几块、多少字节"，
			// 然后 seek 到该偏移继续传，不用从头再来
			std::string progress_key = UploadProgressKey(0, md5);
			if (!md5.empty()) {
				if (seq == 1) {
					auto file_info = std::make_shared<FileInfo>();
					file_info->_file_path_str = (ConfigMgr::Inst().GetFileOutPath() / name).string();
					file_info->_name = name;
					file_info->_seq = seq;
					file_info->_total_size = total_size;
					file_info->_trans_size = trans_size;
					if (!RedisMgr::GetInstance()->SetFileInfo(progress_key, file_info)) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						return;
					}
				}
				else {
					auto file_info = RedisMgr::GetInstance()->GetFileInfo(progress_key);
					if (file_info == nullptr) {
						// 没有进度记录说明这次续传接不上（Redis 过期/服务换过），
						// 让客户端回 FileNotExists 后从头传
						rtvalue["error"] = ErrorCodes::FileNotExists;
						return;
					}
					file_info->_seq = seq;
					file_info->_trans_size = trans_size;
					if (!RedisMgr::GetInstance()->SetFileInfo(progress_key, file_info)) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						return;
					}
				}
			}

			// 按 std::hash(文件名) % FILE_WORKER_COUNT 选择文件线程：
			// 同名文件的所有分块都在同一线程串行写盘，保证顺序
			std::hash<std::string> hash_fn;
			size_t hash_value = hash_fn(name);
			int index = hash_value % FILE_WORKER_COUNT;
			LLOG_INFO << "Hash value: " << hash_value << std::endl;

			auto task = std::make_shared<FileTask>(session, ID_UPLOAD_FILE_REQ, name, seq, total_size,
				trans_size, last, file_data, 0);
			task->_md5 = md5;
			FileSystem::GetInstance()->PostMsgToQue(task, index);

			rtvalue["error"] = ErrorCodes::Success;
			rtvalue["total_size"] = total_size;
			rtvalue["seq"] = seq;
			rtvalue["name"] = name;
			rtvalue["trans_size"] = trans_size;
			rtvalue["last"] = last;
			rtvalue["md5"] = md5;
	};

	// 1005 同步上传进度（day38 断点续传）：客户端点"继续"时先问服务器传到哪了。
	// 请求 {md5, name, uid, token}，回包 {error, md5, name, seq, trans_size, total_size}。
	// 客户端拿到 seq/trans_size 后把本地文件 seek 到 trans_size，从 seq+1 继续发
	_fun_callbacks[ID_SYNC_FILE_REQ] = [this](shared_ptr<CSession> session, const short& msg_id,
		const string& msg_data) {
			Json::Reader reader;
			Json::Value root;
			reader.parse(msg_data, root);
			auto md5 = root["md5"].asString();
			auto name = root["name"].asString();
			auto uid = root["uid"].asInt();

			Json::Value rtvalue;
			Defer defer([this, &rtvalue, session]() {
				std::string return_str = rtvalue.toStyledString();
				session->Send(return_str, ID_SYNC_FILE_RSP);
				});

			rtvalue["md5"] = md5;
			rtvalue["uid"] = uid;
			rtvalue["name"] = name;

			if (md5.empty()) {
				rtvalue["error"] = ErrorCodes::Error_Json;
				return;
			}

			auto file_info = RedisMgr::GetInstance()->GetFileInfo(UploadProgressKey(uid, md5));
			if (file_info == nullptr) {
				// 进度不存在（超时/服务重启/已完成清理）：客户端会从第 1 块重传
				rtvalue["error"] = ErrorCodes::FileNotExists;
				return;
			}

			rtvalue["error"] = ErrorCodes::Success;
			rtvalue["name"] = file_info->_name;
			rtvalue["seq"] = file_info->_seq;
			rtvalue["trans_size"] = static_cast<int>(file_info->_trans_size);
			rtvalue["total_size"] = static_cast<int>(file_info->_total_size);
	};

	// 1031 上传头像分块：与 1003 的差别——
	// 1. 请求里多带 uid/token/last_seq；首块时用 Redis 里的 utoken_<uid> 校验 token，
	//    防止未登录的人往别人目录里塞文件（老师版 day39 同款校验）
	// 2. 回包时机不同：1003 解析完立即回 1004（不等落盘），1031 要等 FileWorker
	//    落盘（最后一块还要更新 DB/Redis）后才回 1032，所以回包逻辑包在 callback 里
	// 3. 每块都把进度写 Redis（key=uid|md5），支持暂停/断线后续传
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
			LLOG_INFO << "1031 parse: seq=" << seq << " data_len=" << file_data.size()
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
					LLOG_INFO << "1031 token invalid: no token in redis, uid=" << uid_str << std::endl;
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
					LLOG_INFO << "1031 token mismatch: got [" << token.substr(0, 8)
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

			// ===== day38 断点续传：每块都写 Redis 进度（key=uid|md5）=====
			if (!md5.empty()) {
				std::string progress_key = UploadProgressKey(uid, md5);
				std::string uid_str = std::to_string(uid);
				auto file_path = ConfigMgr::Inst().GetFileOutPath();
				if (seq == 1) {
					auto file_info = std::make_shared<FileInfo>();
					file_info->_file_path_str = (file_path / uid_str / name).string();
					file_info->_name = name;
					file_info->_seq = seq;
					file_info->_total_size = total_size;
					file_info->_trans_size = trans_size;
					if (!RedisMgr::GetInstance()->SetFileInfo(progress_key, file_info)) {
						Json::Value rtvalue;
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						rtvalue["md5"] = md5;
						rtvalue["seq"] = seq;
						rtvalue["name"] = name;
						rtvalue["uid"] = uid;
						session->Send(rtvalue.toStyledString(), ID_UPLOAD_HEAD_ICON_RSP);
						return;
					}
				}
				else {
					auto file_info = RedisMgr::GetInstance()->GetFileInfo(progress_key);
					if (file_info == nullptr) {
						// 进度丢失：让客户端从第 1 块重传（客户端收到 FileNotExists 会重置）
						Json::Value rtvalue;
						rtvalue["error"] = ErrorCodes::FileNotExists;
						rtvalue["md5"] = md5;
						rtvalue["seq"] = seq;
						rtvalue["name"] = name;
						rtvalue["uid"] = uid;
						session->Send(rtvalue.toStyledString(), ID_UPLOAD_HEAD_ICON_RSP);
						return;
					}
					file_info->_seq = seq;
					file_info->_trans_size = trans_size;
					if (!RedisMgr::GetInstance()->SetFileInfo(progress_key, file_info)) {
						Json::Value rtvalue;
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						rtvalue["md5"] = md5;
						rtvalue["seq"] = seq;
						rtvalue["name"] = name;
						rtvalue["uid"] = uid;
						session->Send(rtvalue.toStyledString(), ID_UPLOAD_HEAD_ICON_RSP);
						return;
					}
				}
			}

			//与 1003 相同：按文件名哈希选 FileWorker，
			//同一头像（同名文件）的所有分块落到同一线程串行落盘
			std::hash<std::string> hash_fn;
			size_t hash_value = hash_fn(name);
			int index = hash_value % FILE_WORKER_COUNT;
			// 临时调试：投递时打印 seq 与目标 worker，对照 worker 执行日志
			LLOG_INFO << "1031 dispatch: seq=" << seq << " index=" << index
				<< " name=[" << name << "]" << std::endl;

			auto task = std::make_shared<FileTask>(session, ID_UPLOAD_HEAD_ICON_REQ, name, seq,
				total_size, trans_size, last, file_data, uid);
			task->_md5 = md5;
			task->_callback = callback;
			FileSystem::GetInstance()->PostMsgToQue(task, index);
	};

	// 1033 下载文件（头像/聊天图片/聊天文件共用）。
	// 请求 {name, seq, token, uid(请求者), owner_uid(文件所有者), client_path(客户端保存路径)}
	// 回包 {error, data(base64分块), seq, total_size, current_size, is_last, name, client_path}
	//
	// day38 断点续传：读取动作交给 DownloadWorker（按文件名哈希选线程），
	// 由它把"下一块序号/已下载字节数"写 Redis。客户端暂停/断线后带着本地 .tmp 的
	// 偏移重新请求 seq=N，服务器能判断序号是否接得上，接不上就让客户端从 1 重下。
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

			// 失败回包：把定位字段一并带回，客户端靠 client_path 找回下载上下文
			auto send_error = [session, name, client_path, seq](int err) {
				Json::Value rtvalue;
				rtvalue["error"] = err;
				rtvalue["name"] = name;
				rtvalue["client_path"] = client_path;
				rtvalue["seq"] = seq;
				session->Send(rtvalue.toStyledString(), ID_DOWN_LOAD_FILE_RSP);
			};

			//登录态校验：防止任何人凭文件名扫描下载他人文件
			if (seq == 1) {
				std::string token_key = USERTOKENPREFIX + std::to_string(uid);
				std::string token_value = "";
				bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
				if (!success) {
					send_error(ErrorCodes::UidInvalid);
					return;
				}
				if (token_value != token) {
					send_error(ErrorCodes::TokenInvalid);
					return;
				}
			}

			//文件名安全校验：name 只应是纯文件名，带路径分隔符直接拒绝，
			//防止 "../" 之类的目录穿越读任意文件
			if (name.empty() || name.find('/') != std::string::npos
				|| name.find("\\") != std::string::npos
				|| name.find("..") != std::string::npos) {
				send_error(ErrorCodes::FileNotExists);
				return;
			}
			if (seq < 1) {
				send_error(ErrorCodes::FileSeqInvalid);
				return;
			}

			auto file_path = ConfigMgr::Inst().GetFileOutPath();
			auto file_path_str = (file_path / std::to_string(owner_uid) / name).string();

			// 先做一次存在性检查，省得把注定失败的请求投进下载线程
			std::ifstream infile(file_path_str, std::ios::binary);
			if (!infile) {
				LLOG_ERROR << "download file not exists: " << file_path_str << std::endl;
				send_error(ErrorCodes::FileNotExists);
				return;
			}
			infile.close();

			std::string download_key = DownloadProgressKey(owner_uid, name);
			// 回调在 DownloadWorker 线程里执行：session->Send 内部有锁，跨线程安全
			auto callback = [session, name, client_path, seq](const Json::Value& result) {
				Json::Value rtvalue = result;
				rtvalue["name"] = name;
				rtvalue["client_path"] = client_path;
				rtvalue["seq"] = seq;
				session->Send(rtvalue.toStyledString(), ID_DOWN_LOAD_FILE_RSP);
			};

			auto task = std::make_shared<DownloadTask>(
				download_key, file_path_str, name, seq, callback);
			std::hash<std::string> hash_fn;
			int index = hash_fn(name) % DOWN_LOAD_WORKER_COUNT;
			FileSystem::GetInstance()->PostDownloadTaskToQue(task, index);
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
			// 公共字段先填：错误回包也要带 name/md5/seq，客户端据此找回上传上下文
			rtvalue["md5"] = md5;
			rtvalue["name"] = name;
			rtvalue["seq"] = seq;
			rtvalue["total_size"] = total_size;
			rtvalue["trans_size"] = trans_size;
			rtvalue["last"] = last;
			rtvalue["message_id"] = message_id;
			rtvalue["sender"] = sender;
			rtvalue["receiver"] = receiver;

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

			// ===== 秒传：md5 命中服务器已有文件，直接硬链接到本次目标路径 =====
			// 同一份文件第二次发送时，服务器不再收一遍分块
			if (!md5.empty() && !name.empty()) {
				const std::string target_path = (ConfigMgr::Inst().GetFileOutPath()
					/ std::to_string(sender) / name).string();
				boost::system::error_code fs_ec;
				bool target_ready = false;
				if (boost::filesystem::exists(target_path, fs_ec) && !fs_ec) {
					auto target_size = boost::filesystem::file_size(target_path, fs_ec);
					target_ready = !fs_ec && (int64_t)target_size == (int64_t)total_size;
				}

				// seq>1 的在途分块：秒传任务已经受理，直接回成功，
				// 避免客户端把 "进度不存在" 当成断点丢失又重传一遍
				std::string instant_mark;
				bool instant_uploading = RedisMgr::GetInstance()->Get(
					FILE_INSTANT_PREFIX + std::to_string(sender) + "|" + md5, instant_mark);
				if (target_ready || (seq > 1 && instant_uploading)) {
					rtvalue["error"] = ErrorCodes::Success;
					rtvalue["instant"] = true;
					rtvalue["trans_size"] = total_size;
					rtvalue["last"] = 1;
					return;
				}

				if (seq == 1) {
					std::string cached_path;
					bool cached_ok = false;

					// 1) 先查 Redis 缓存
					bool has_cached = RedisMgr::GetInstance()->Get(
						FILE_MD5_PREFIX + md5, cached_path);
					if (has_cached && boost::filesystem::exists(cached_path, fs_ec) && !fs_ec) {
						auto cached_size = boost::filesystem::file_size(cached_path, fs_ec);
						cached_ok = !fs_ec && (int64_t)cached_size == (int64_t)total_size;
					}
					if (has_cached && !cached_ok) {
						// 缓存指向的文件已被删/大小不对：清掉，继续查 MySQL
						RedisMgr::GetInstance()->Del(FILE_MD5_PREFIX + md5);
					}

					// 2) Redis 未命中/失效：查 MySQL（Redis 重启后依然能秒传）
					if (!cached_ok) {
						FileObjectInfo obj;
						if (MysqlMgr::GetInstance()->GetFileObject(md5, obj)
							&& (int64_t)obj.file_size == (int64_t)total_size) {
							std::string abs_path = (
								ConfigMgr::Inst().GetFileOutPath() / obj.file_path).string();
							boost::system::error_code db_ec;
							if (boost::filesystem::exists(abs_path, db_ec) && !db_ec) {
								auto sz = boost::filesystem::file_size(abs_path, db_ec);
								if (!db_ec && (int64_t)sz == (int64_t)total_size) {
									cached_path = abs_path;
									cached_ok = true;
									// 回填 Redis，下次走缓存
									RedisMgr::GetInstance()->Set(
										FILE_MD5_PREFIX + md5, abs_path);
								}
							}
							if (!cached_ok) {
								// DB 记录指向的文件已不存在：删掉坏记录，走普通上传
								MysqlMgr::GetInstance()->DeleteFileObject(md5);
							}
						}
					}

					if (cached_ok) {
						// 短 TTL 标记：本次上传后续 seq 来了也直接回成功
						RedisMgr::GetInstance()->SetExp(
							FILE_INSTANT_PREFIX + std::to_string(sender) + "|" + md5,
							name, 120);

						std::hash<std::string> hash_fn;
						int index = int(hash_fn(name) % FILE_WORKER_COUNT);
						auto task = std::make_shared<FileTask>(session,
							ID_IMG_CHAT_UPLOAD_REQ, name, seq, total_size,
							total_size, 1, "", sender);
						task->_md5 = md5;
						task->_chat_msg_id = message_id;
						task->_receiver = receiver;
						task->_instant = true;
						task->_instant_source = cached_path;
						FileSystem::GetInstance()->PostMsgToQue(task, index);

						rtvalue["error"] = ErrorCodes::Success;
						rtvalue["instant"] = true;
						rtvalue["trans_size"] = total_size;
						rtvalue["last"] = 1;
						return;
					}
				}
			}

			// ===== day38 断点续传：每块都写 Redis 进度（key=发送者uid|md5）=====
			// 与 1031 同款：暂停/断线后客户端发 1005 就能拿回续传起点
			if (!md5.empty()) {
				std::string progress_key = UploadProgressKey(sender, md5);
				if (seq == 1) {
					auto file_info = std::make_shared<FileInfo>();
					file_info->_file_path_str = (ConfigMgr::Inst().GetFileOutPath()
						/ std::to_string(sender) / name).string();
					file_info->_name = name;
					file_info->_seq = seq;
					file_info->_total_size = total_size;
					file_info->_trans_size = trans_size;
					if (!RedisMgr::GetInstance()->SetFileInfo(progress_key, file_info)) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						return;
					}
				}
				else {
					auto file_info = RedisMgr::GetInstance()->GetFileInfo(progress_key);
					if (file_info == nullptr) {
						rtvalue["error"] = ErrorCodes::FileNotExists;
						return;
					}
					file_info->_seq = seq;
					file_info->_trans_size = trans_size;
					if (!RedisMgr::GetInstance()->SetFileInfo(progress_key, file_info)) {
						rtvalue["error"] = ErrorCodes::FileSaveRedisFailed;
						return;
					}
				}
			}

			//按文件名哈希选 FileWorker：同一图片的所有分块串行落盘（保证顺序）
			std::hash<std::string> hash_fn;
			size_t hash_value = hash_fn(name);
			int index = hash_value % FILE_WORKER_COUNT;

			auto task = std::make_shared<FileTask>(session, ID_IMG_CHAT_UPLOAD_REQ, name, seq,
				total_size, trans_size, last, file_data, sender);
			task->_md5 = md5;
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
			rtvalue["md5"] = md5;
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
