#include "FileSystem.h"
#include "ConfigMgr.h"
#include "CSession.h"
#include "base64.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "ChatServerGrpcClient.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

FileWorker::FileWorker() :_b_stop(false)
{
	RegisterHandlers();

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

FileWorker::~FileWorker()
{
	_b_stop = true;
	_cv.notify_one();
	_work_thread.join();
}

void FileWorker::PostTask(std::shared_ptr<FileTask> task)
{
	{
		std::lock_guard<std::mutex> lock(_mtx);
		_task_que.push(task);
	}

	_cv.notify_one();
}

//按来源消息 ID 查 handler 表分发；没注册的 id 直接丢弃并打日志
void FileWorker::task_callback(std::shared_ptr<FileTask> task)
{
	auto call_back_iter = _handlers.find(task->_msgid);
	if (call_back_iter == _handlers.end()) {
		std::cout << "file msg id [" << task->_msgid << "] handler not found" << std::endl;
		return;
	}
	call_back_iter->second(task);
}

void FileWorker::RegisterHandlers()
{
	// 1003 通用文件分块落盘：base64 解码后按 seq 追加写入文件。
	// seq==1 打开文件并清空（新建），其余追加。
	// 因为同一文件的分块保证在同一线程串行处理，不会出现写乱序
	_handlers[ID_UPLOAD_FILE_REQ] = [this](std::shared_ptr<FileTask> task) {
		// 解码 base64 得到原始二进制分块
		std::string decoded = base64_decode(task->_file_data);

		auto file_path = ConfigMgr::Inst().GetFileOutPath();
		auto file_path_str = (file_path / task->_name).string();
		auto last = task->_last;
		std::ofstream outfile;
		//第一个包：打开文件，如果存在则清空，不存在则创建
		if (task->_seq == 1) {
			outfile.open(file_path_str, std::ios::binary | std::ios::trunc);
		}
		else {
			// 保存为文件
			outfile.open(file_path_str, std::ios::binary | std::ios::app);
		}


		if (!outfile) {
			std::cerr << "无法打开文件进行写入。" << std::endl;
			return;
		}

		outfile.write(decoded.data(), decoded.size());
		if (!outfile) {
			std::cerr << "写入文件失败。" << std::endl;
			return;
		}

		outfile.close();
		if (last) {
			std::cout << "文件已成功保存为: " << task->_name
				<< " total_size=" << task->_total_size << std::endl;
		}
	};

	// 1031 头像分块落盘：写盘部分与 1003 相同，差别有两点——
	// 1. 落盘路径多一层 uid 子目录：output/static/<uid>/<name>，
	//    不同用户的头像互不覆盖，文件名（如 head.png）得以固定复用
	// 2. 最后一块处理完后要更新 MySQL user.icon 并写回 Redis 缓存，
	//    然后才通过 callback 回包 1032（落盘+DB 都完成了才告诉客户端成功）
	_handlers[ID_UPLOAD_HEAD_ICON_REQ] = [this](std::shared_ptr<FileTask> task) {
		// 解码
		std::string decoded = base64_decode(task->_file_data);

		auto file_path = ConfigMgr::Inst().GetFileOutPath();
		auto file_path_str = (file_path / std::to_string(task->_uid) / task->_name).string();
		auto last = task->_last;

		boost::filesystem::path file_path_boost(file_path_str);
		boost::filesystem::path dir_path = file_path_boost.parent_path();
		// 获取完整文件名（包含扩展名）——它就是写进 DB user.icon 的值
		std::string filename = file_path_boost.filename().string();
		Json::Value result;
		result["error"] = ErrorCodes::Success;

		// uid 子目录不存在则创建（首次上传时 output/static/<uid>/ 还没有）
		if (!boost::filesystem::exists(dir_path)) {
			if (!boost::filesystem::create_directories(dir_path)) {
				std::cerr << "Failed to create directory: " << dir_path.string() << std::endl;
				result["error"] = ErrorCodes::FileWritePermissionFailed;
				if (task->_callback) {
					task->_callback(result);
				}
				return;
			}
		}

		std::ofstream outfile;
		//第一个包：打开文件，如果存在则清空，不存在则创建（重传头像即覆盖旧图）
		if (task->_seq == 1) {
			outfile.open(file_path_str, std::ios::binary | std::ios::trunc);
		}
		else {
			outfile.open(file_path_str, std::ios::binary | std::ios::app);
		}

		if (!outfile) {
			std::cerr << "无法打开文件进行写入。" << std::endl;
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			if (task->_callback) {
				task->_callback(result);
			}
			return;
		}

		outfile.write(decoded.data(), decoded.size());
		if (!outfile) {
			std::cerr << "写入文件失败。" << std::endl;
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			if (task->_callback) {
				task->_callback(result);
			}
			return;
		}

		outfile.close();

		if (last) {
			std::cout << "头像已成功保存为: " << file_path_str << std::endl;

			//最后一块落盘完成后：更新 MySQL user.icon
			MysqlMgr::GetInstance()->UpdateUserIcon(task->_uid, filename);

			//再整行读出用户信息写回 Redis 缓存（与 ChatServer 写缓存格式一致）：
			//这样下次登录/好友列表拉取时拿到的就是新头像文件名
			auto user_info = MysqlMgr::GetInstance()->GetUser(task->_uid);
			if (user_info == nullptr) {
				return;
			}

			Json::Value redis_root;
			redis_root["uid"] = task->_uid;
			redis_root["pwd"] = user_info->pwd;
			redis_root["name"] = user_info->name;
			redis_root["email"] = user_info->email;
			redis_root["nick"] = user_info->nick;
			redis_root["desc"] = user_info->desc;
			redis_root["sex"] = user_info->sex;
			redis_root["icon"] = user_info->icon;
			std::string base_key = USER_BASE_INFO + std::to_string(task->_uid);
			RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
		}

		if (task->_callback) {
			task->_callback(result);
		}
	};

	// 1037 聊天图片落盘：路径 static/<发送者uid>/<unique_name>（与 1031 头像同构，
	// 下载时 1033 带 owner_uid=发送者 正好命中）。最后一块落盘后做两件事：
	// 1. UpdateUploadStatus：图片消息入库时 status=0（资源未传完），这里置 2
	// 2. 查收图人所在节点（Redis uip_<receiver>）→ gRPC NotifyChatImgMsg 让对方
	//    ChatServer 推 1039 ——通知放在"文件传完后"是老师版的关键设计：
	//    对方收到通知时图片已就绪，立即下载不会 404
	_handlers[ID_IMG_CHAT_UPLOAD_REQ] = [this](std::shared_ptr<FileTask> task) {
		// 解码
		std::string decoded = base64_decode(task->_file_data);

		auto file_path = ConfigMgr::Inst().GetFileOutPath();
		auto file_path_str = (file_path / std::to_string(task->_uid) / task->_name).string();
		auto last = task->_last;

		boost::filesystem::path file_path_boost(file_path_str);
		boost::filesystem::path dir_path = file_path_boost.parent_path();
		Json::Value result;
		result["error"] = ErrorCodes::Success;

		//发送者 uid 子目录不存在则创建（该用户第一次发图时）
		if (!boost::filesystem::exists(dir_path)) {
			if (!boost::filesystem::create_directories(dir_path)) {
				std::cerr << "Failed to create directory: " << dir_path.string() << std::endl;
				result["error"] = ErrorCodes::FileWritePermissionFailed;
				return;
			}
		}

		std::ofstream outfile;
		//第一个包：打开文件，如果存在则清空，不存在则创建
		if (task->_seq == 1) {
			outfile.open(file_path_str, std::ios::binary | std::ios::trunc);
		}
		else {
			outfile.open(file_path_str, std::ios::binary | std::ios::app);
		}

		if (!outfile) {
			std::cerr << "无法打开文件进行写入。" << std::endl;
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			return;
		}

		outfile.write(decoded.data(), decoded.size());
		if (!outfile) {
			std::cerr << "写入文件失败。" << std::endl;
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			return;
		}

		outfile.close();

		if (last) {
			std::cout << "聊天图片已成功保存为: " << file_path_str
				<< " message_id=" << task->_chat_msg_id << std::endl;

			//文件完整落盘：把图片消息状态置为已上传
			MysqlMgr::GetInstance()->UpdateUploadStatus(task->_chat_msg_id);

			//查收图人当前登录的节点名（登录时 Redis 写入），未登录就不用推通知
			std::string uid_ip_value = "";
			auto uid_ip_key = USERIPPREFIX + std::to_string(task->_receiver);
			bool b_ip = RedisMgr::GetInstance()->Get(uid_ip_key, uid_ip_value);
			if (!b_ip) {
				return;
			}

			//gRPC 通知对方所在 ChatServer → 由它推 1039 给收图人客户端
			ChatServerGrpcClient::GetInstance()->NotifyChatImgMsg(task->_chat_msg_id, uid_ip_value);
		}
	};
}

FileSystem::~FileSystem()
{

}

void FileSystem::PostMsgToQue(shared_ptr<FileTask> msg, int index)
{
	_file_workers[index]->PostTask(msg);
}

FileSystem::FileSystem()
{
	for (int i = 0; i < FILE_WORKER_COUNT; i++) {
		_file_workers.push_back(std::make_shared<FileWorker>());
	}
}
