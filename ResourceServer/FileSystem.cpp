#include "FileSystem.h"
#include "Logger.h"
#include "ConfigMgr.h"
#include "CSession.h"
#include "base64.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "ChatServerGrpcClient.h"
#include <boost/filesystem.hpp>
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
		LLOG_INFO << "file msg id [" << task->_msgid << "] handler not found" << std::endl;
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
			LLOG_ERROR << "无法打开文件进行写入。" << std::endl;
			return;
		}

		outfile.write(decoded.data(), decoded.size());
		if (!outfile) {
			LLOG_ERROR << "写入文件失败。" << std::endl;
			return;
		}

		outfile.close();
		if (last) {
			LLOG_INFO << "文件已成功保存为: " << task->_name
				<< " total_size=" << task->_total_size << std::endl;
		}
	};

	// 1031 头像分块落盘：路径多一层 uid 子目录（output/static/<uid>/<name>），
	// 不同用户的头像互不覆盖；最后一块落盘后更新 MySQL user.icon、Redis 缓存并广播 1057。
	//
	// day38 断点续传改造：不再"整文件缓存在内存里等最后一块拼装"，改为
	//   先追加写 <name>.<md5>.part，最后一块到达时校验大小再 rename 成正式文件。
	// 这样断开/暂停后从 Redis 记录的偏移继续 append 就能续上；
	// 正式文件只会在完整收齐后出现，下载方不会读到半张图（原子替换旧头像）。
	// .part 文件名带 md5：同一用户并发上传同名文件时互不截断。
	_handlers[ID_UPLOAD_HEAD_ICON_REQ] = [this](std::shared_ptr<FileTask> task) {
		// 解码
		std::string decoded = base64_decode(task->_file_data);

		auto file_path = ConfigMgr::Inst().GetFileOutPath();
		auto file_path_str = (file_path / std::to_string(task->_uid) / task->_name).string();
		auto last = task->_last;
		auto filename = task->_name;
		Json::Value result;
		result["error"] = ErrorCodes::Success;

		// uid 子目录不存在则创建（首次上传时 output/static/<uid>/ 还没有）
		boost::filesystem::path dir_path = boost::filesystem::path(file_path_str).parent_path();
		if (!boost::filesystem::exists(dir_path)) {
			if (!boost::filesystem::create_directories(dir_path)) {
				LLOG_ERROR << "Failed to create directory: " << dir_path.string() << std::endl;
				result["error"] = ErrorCodes::FileWritePermissionFailed;
				if (task->_callback) {
					task->_callback(result);
				}
				return;
			}
		}

		// 续传临时文件：同一次上传（md5 相同）固定同一个 .part，服务重启也能接着写
		std::string part_path = file_path_str + "."
			+ (task->_md5.empty() ? "upload" : task->_md5) + ".part";
		std::ofstream outfile;
		if (task->_seq == 1) {
			// 新上传：清空旧 .part，从 0 开始
			outfile.open(part_path, std::ios::binary | std::ios::trunc);
		}
		else {
			// 续传：接着已落盘的内容追加
			outfile.open(part_path, std::ios::binary | std::ios::app);
		}

		if (!outfile) {
			LLOG_ERROR << "1031 无法打开续传文件: " << part_path << std::endl;
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			if (task->_callback) {
				task->_callback(result);
			}
			return;
		}

		outfile.write(decoded.data(), decoded.size());
		outfile.close();
		if (!outfile) {
			LLOG_ERROR << "1031 写入续传文件失败: " << part_path << std::endl;
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			if (task->_callback) {
				task->_callback(result);
			}
			return;
		}

		if (!last) {
			if (task->_callback) {
				task->_callback(result);
			}
			return;
		}

		// 最后一块：校验 .part 总大小，对不上说明有分块缺失/重复，不能发布成正式头像
		boost::system::error_code ec;
		auto part_size = boost::filesystem::file_size(part_path, ec);
		if (ec || (int64_t)part_size != (int64_t)task->_total_size) {
			LLOG_ERROR << "1031 续传文件大小不匹配: got "
				<< (ec ? std::string("error") : std::to_string(part_size))
				<< " expect " << task->_total_size << " (" << part_path << ")" << std::endl;
			// 比预期大说明数据坏了（重复块），直接删掉让客户端重传；比预期小则保留 .part 等续传
			if (!ec && (int64_t)part_size > (int64_t)task->_total_size) {
				boost::filesystem::remove(part_path, ec);
			}
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			if (task->_callback) {
				task->_callback(result);
			}
			return;
		}

		// 原子替换：同一文件系统内 rename 覆盖旧头像，下载方永远只会看到完整文件
		boost::filesystem::rename(part_path, file_path_str, ec);
		if (ec) {
			// Windows 上目标存在时 rename 会失败，退化为先删再改名（Linux 项目基本走不到）
			boost::filesystem::remove(file_path_str, ec);
			ec.clear();
			boost::filesystem::rename(part_path, file_path_str, ec);
		}
		if (ec) {
			LLOG_ERROR << "1031 发布头像失败: " << file_path_str
				<< " err=" << ec.message() << std::endl;
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			if (task->_callback) {
				task->_callback(result);
			}
			return;
		}

		LLOG_INFO << "头像已成功保存为: " << file_path_str << std::endl;

		//最后一块落盘完成后：更新 MySQL user.icon
		MysqlMgr::GetInstance()->UpdateUserIcon(task->_uid, filename);

		//再整行读出用户信息写回 Redis 缓存（与 ChatServer 写缓存格式一致）：
		//这样下次登录/好友列表拉取时拿到的就是新头像文件名
		auto user_info = MysqlMgr::GetInstance()->GetUser(task->_uid);
		if (user_info == nullptr) {
			if (task->_callback) {
				task->_callback(result);
			}
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

		//头像变更广播：DB/Redis 都更新完后，gRPC 广播给两个聊天节点，
		//节点推 1057 给全部在线用户，客户端收到后删本地缓存重新下载（在线同步回显）
		//文件名一并下发：接收方本地可能还停在默认编号（"1"），不知道新文件名就无从下载
		ChatServerGrpcClient::GetInstance()->NotifyAvatarUpdate(task->_uid, filename);

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
				LLOG_ERROR << "Failed to create directory: " << dir_path.string() << std::endl;
				result["error"] = ErrorCodes::FileWritePermissionFailed;
				return;
			}
		}

		// 与 1031 同款续传写盘：分块先追加到 <name>.<md5>.part，
		// 最后一块校验大小后 rename 成正式文件。断线/暂停后从 Redis 偏移继续 append；
		// 正式文件只会在完整收齐后出现，接收方不会下到半张图
		std::string part_path = file_path_str + "."
			+ (task->_md5.empty() ? "upload" : task->_md5) + ".part";
		std::ofstream outfile;
		if (task->_seq == 1) {
			outfile.open(part_path, std::ios::binary | std::ios::trunc);
		}
		else {
			outfile.open(part_path, std::ios::binary | std::ios::app);
		}

		if (!outfile) {
			LLOG_ERROR << "1037 无法打开续传文件: " << part_path << std::endl;
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			return;
		}

		outfile.write(decoded.data(), decoded.size());
		outfile.close();
		if (!outfile) {
			LLOG_ERROR << "1037 写入续传文件失败: " << part_path << std::endl;
			result["error"] = ErrorCodes::FileWritePermissionFailed;
			return;
		}

		if (!last) {
			return;
		}

		//最后一块：先校验 .part 大小，完整才发布成正式文件
		boost::system::error_code ec;
		auto part_size = boost::filesystem::file_size(part_path, ec);
		if (ec || (int64_t)part_size != (int64_t)task->_total_size) {
			LLOG_ERROR << "1037 续传文件大小不匹配: got "
				<< (ec ? std::string("error") : std::to_string(part_size))
				<< " expect " << task->_total_size << " (" << part_path << ")" << std::endl;
			if (!ec && (int64_t)part_size > (int64_t)task->_total_size) {
				boost::filesystem::remove(part_path, ec);
			}
			return;
		}

		boost::filesystem::rename(part_path, file_path_str, ec);
		if (ec) {
			boost::filesystem::remove(file_path_str, ec);
			ec.clear();
			boost::filesystem::rename(part_path, file_path_str, ec);
		}
		if (ec) {
			LLOG_ERROR << "1037 发布聊天资源失败: " << file_path_str
				<< " err=" << ec.message() << std::endl;
			return;
		}

		LLOG_INFO << "聊天图片已成功保存为: " << file_path_str
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
	};
}

// ===================== DownloadWorker =====================

DownloadWorker::DownloadWorker() :_b_stop(false)
{
	_work_thread = std::thread([this]() {
		while (!_b_stop) {
			std::unique_lock<std::mutex> lock(_mtx);
			_cv.wait(lock, [this]() {
				return _b_stop || !_task_que.empty();
			});

			if (_b_stop) {
				return;
			}

			auto task = _task_que.front();
			_task_que.pop();
			lock.unlock();
			task_callback(task);
		}
	});
}

DownloadWorker::~DownloadWorker()
{
	_b_stop = true;
	_cv.notify_one();
	_work_thread.join();
}

void DownloadWorker::PostTask(std::shared_ptr<DownloadTask> task)
{
	{
		std::lock_guard<std::mutex> lock(_mtx);
		_task_que.push(task);
	}
	_cv.notify_one();
}

//按 seq 从文件读一块：seq==1 时重新建立下载进度（覆盖旧记录），
//后续 seq 必须与 Redis 里记录的下一个序号一致，否则回 FileSeqInvalid，
//客户端收到后会删掉本地 .tmp 从第 1 块重新下（避免拼出坏文件）
void DownloadWorker::task_callback(std::shared_ptr<DownloadTask> task)
{
	Json::Value result;
	result["error"] = ErrorCodes::Success;
	result["seq"] = task->_seq;
	result["name"] = task->_name;

	std::ifstream infile(task->_path, std::ios::binary);
	if (!infile) {
		LLOG_ERROR << "download file not exists: " << task->_path << std::endl;
		result["error"] = ErrorCodes::FileNotExists;
		if (task->_callback) {
			task->_callback(result);
		}
		return;
	}

	std::shared_ptr<FileInfo> file_info;
	if (task->_seq == 1) {
		infile.seekg(0, std::ios::end);
		std::streamsize total_size = infile.tellg();
		infile.seekg(0, std::ios::beg);

		file_info = std::make_shared<FileInfo>();
		file_info->_file_path_str = task->_path;
		file_info->_name = task->_name;
		file_info->_seq = 1;
		file_info->_total_size = total_size;
		file_info->_trans_size = 0;
		// 新下载：覆盖旧进度（同一个文件可能上次只下到一半）
		RedisMgr::GetInstance()->SetDownloadInfo(task->_key, file_info);
		LLOG_INFO << "[新下载] " << task->_name << " size=" << total_size << std::endl;
	}
	else {
		file_info = RedisMgr::GetInstance()->GetDownloadInfo(task->_key);
		if (file_info == nullptr) {
			LLOG_ERROR << "断点续传失败，Redis 无下载信息: " << task->_key << std::endl;
			result["error"] = ErrorCodes::RedisReadErr;
			if (task->_callback) {
				task->_callback(result);
			}
			return;
		}
		if (task->_seq != file_info->_seq) {
			LLOG_ERROR << "下载序号不匹配: expect " << file_info->_seq
				<< " got " << task->_seq << " (" << task->_key << ")" << std::endl;
			result["error"] = ErrorCodes::FileSeqInvalid;
			if (task->_callback) {
				task->_callback(result);
			}
			return;
		}
	}

	// 计算偏移量：seq 从 1 开始，偏移 = (seq-1) * 32KB
	std::streamsize offset = ((std::streamsize)task->_seq - 1) * FILE_CHUNK_LEN;
	if (offset >= file_info->_total_size) {
		LLOG_ERROR << "下载偏移越界: offset=" << offset
			<< " total=" << file_info->_total_size << std::endl;
		result["error"] = ErrorCodes::FileOffsetInvalid;
		if (task->_callback) {
			task->_callback(result);
		}
		return;
	}

	infile.seekg(offset);
	char buffer[FILE_CHUNK_LEN];
	infile.read(buffer, FILE_CHUNK_LEN);
	std::streamsize bytes_read = infile.gcount();
	infile.close();

	if (bytes_read <= 0) {
		result["error"] = ErrorCodes::FileReadFailed;
		if (task->_callback) {
			task->_callback(result);
		}
		return;
	}

	std::string data_to_encode(buffer, bytes_read);
	std::streamsize current_pos = offset + bytes_read;
	bool is_last = (current_pos >= file_info->_total_size);

	result["data"] = base64_encode(data_to_encode);
	result["total_size"] = std::to_string(file_info->_total_size);
	result["current_size"] = std::to_string(current_pos);
	result["is_last"] = is_last;

	if (is_last) {
		// 整份文件发完：删掉下载进度（下次下载重新从 seq=1 开始）
		RedisMgr::GetInstance()->DelDownloadInfo(task->_key);
		LLOG_INFO << "文件下载完成: " << task->_path << std::endl;
	}
	else {
		// 记录下一块序号与已读字节数，供暂停/断线后续传
		file_info->_seq = task->_seq + 1;
		file_info->_trans_size = current_pos;
		RedisMgr::GetInstance()->SetDownloadInfo(task->_key, file_info);
	}

	if (task->_callback) {
		task->_callback(result);
	}
}

FileSystem::~FileSystem()
{

}

void FileSystem::PostMsgToQue(shared_ptr<FileTask> msg, int index)
{
	// 临时调试：入队时打印 worker 编号与块序号
	LLOG_INFO << "worker[" << index << "] post seq=" << msg->_seq << std::endl;
	_file_workers[index]->PostTask(msg);
}

void FileSystem::PostDownloadTaskToQue(std::shared_ptr<DownloadTask> msg, int index)
{
	_download_workers[index]->PostTask(msg);
}

FileSystem::FileSystem()
{
	for (int i = 0; i < FILE_WORKER_COUNT; i++) {
		_file_workers.push_back(std::make_shared<FileWorker>());
	}

	for (int i = 0; i < DOWN_LOAD_WORKER_COUNT; i++) {
		_download_workers.push_back(std::make_shared<DownloadWorker>());
	}
}
