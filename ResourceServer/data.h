#pragma once
#include <string>
#include <memory>
#include <functional>
#include <json/value.h>
#include "const.h"
#include "MsgNode.h"   // LogicNode 里用到 RecvNode
using namespace std;
class CSession;

//一条待处理的逻辑任务：哪个连接 + 收到的完整包体
struct LogicNode {
	friend class LogicWorker;  //task_callback 要访问私有成员
	LogicNode(shared_ptr<CSession> session, shared_ptr<RecvNode> recvnode);
	shared_ptr<CSession> _session;
	shared_ptr<RecvNode> _recvnode;
};

//用户信息（MySQL user 表的一行）：头像上传收尾时
//先 UPDATE user.icon，再整行读出来写回 Redis 缓存（ubaseinfo_<uid>）
struct UserInfo {
	UserInfo() :name(""), pwd(""), uid(0), email(""), nick(""), desc(""), sex(0), icon(""), back("") {}
	std::string name;
	std::string pwd;
	int uid;
	std::string email;
	std::string nick;
	std::string desc;
	int sex;
	std::string icon;
	std::string back;
};

//一条图片聊天消息（MySQL chat_message 表的一行，精简版）：
//图片文件传完后用它拿消息详情（发件人/收件人/文件名）推 gRPC 通知
struct ChatMessage {
	int message_id = 0;
	int sender_id = 0;
	int recv_id = 0;
	int thread_id = 0;
	std::string content;    //图片消息存唯一文件名（unique_name）
	std::string chat_time;
	int status = 0;
	int msg_type = 0;
};

//文件上传/下载进度：day38 断点续传用，JSON 序列化后存 Redis
//上传 key = "file_upload_<uid>|<md5>"，下载 key = "file_download_<owner_uid>|<name>"
//带 uid 是为了隔离不同用户上传同一份文件（md5 相同）时的进度
struct FileInfo {
	std::string _file_path_str;   //最终落盘路径
	std::string _name;            //文件名
	int _seq = 0;                 //已处理到的分块序号（上传=已收，下载=已发）
	int64_t _total_size = 0;      //文件总字节数
	int64_t _trans_size = 0;      //已处理的字节数（进度条/续传偏移都靠它）
};

//一次下载任务：1033 请求解析后投递给 DownloadWorker，
//真正的读文件/更新 Redis 进度在下载线程里做，logic 线程不阻塞
struct DownloadTask {
	DownloadTask(std::string key, std::string path, std::string name, int seq,
		std::function<void(const Json::Value&)> callback)
		:_key(key), _path(path), _name(name), _seq(seq), _callback(callback)
	{}
	std::string _key;     //Redis 进度 key（owner_uid|name，按文件所有者隔离同名文件）
	std::string _path;    //服务器上的源文件路径
	std::string _name;    //文件名（回包用）
	int _seq;             //本次要读的分块序号（从 1 开始）
	std::function<void(const Json::Value&)> _callback;  //读完/失败后回调回包
};

//一次分块上传任务：从 1003/1031/1037 请求里解析出来的字段 + 解码前的 base64 数据
//_msgid 区分消息类型：FileWorker 按 _msgid 查 handler 表分发
//（1003 纯落盘 / 1031 落盘+DB+Redis / 1037 落盘+状态更新+gRPC 通知）
//_callback 只在 1031 用：FileWorker 落盘完成后回调，由它在 FileWorker 线程里发 1032 回包
//（1003/1037 的回包是 LogicWorker 解析完立即回的，不等落盘，所以不需要 callback）
struct FileTask {
	FileTask(shared_ptr<CSession> session, short msgid, string name,
		int seq, int total_size, int trans_size, int last,
		string file_data, int uid)
		:_session(session), _msgid(msgid),
		_seq(seq), _name(name), _total_size(total_size),
		_trans_size(trans_size), _last(last), _file_data(file_data),
		_uid(uid)
	{}
	shared_ptr<CSession> _session;  //回执要发回给这个连接
	short _msgid;        //来源消息 ID（1003/1031/1037），FileWorker 据此选择处理逻辑
	string _md5;         //文件 md5：作为续传进度 key，也用来给 .part 临时文件命名
	                     //（同一用户并发上传同名文件时，md5 不同则临时文件不同，不会互相截断）
	int _seq;            //分块序号（从 1 开始）
	string _name;        //文件名（落盘文件名）
	int _total_size;     //文件总大小
	int _trans_size;     //当前已传输大小（进度条用）
	int _last;           //是否最后一块
	string _file_data;   //base64 编码的分块数据
	int _uid;            //上传者 uid（1031=头像所有者；1037=图片发送者，落盘子目录用；1003 填 0）
	std::function<void(const Json::Value&)> _callback; //落盘完成回调（1031 回包 1032 用，其余为空）

	// ===== 仅 1037 图片聊天上传使用 =====
	int _chat_msg_id = 0; //对应 chat_message 表的 message_id（文件传完后置状态 + 推通知）
	int _receiver = 0;    //收图人 uid（传完后查它所在节点推 gRPC 通知）
};
