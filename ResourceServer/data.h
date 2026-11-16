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
