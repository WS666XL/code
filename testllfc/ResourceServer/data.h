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

//一次分块上传任务：从 1003/1031 请求里解析出来的字段 + 解码前的 base64 数据
//_msgid 区分消息类型：FileWorker 按 _msgid 查 handler 表分发（1003 纯落盘 / 1031 落盘+DB+Redis）
//_callback 只在 1031 用：FileWorker 落盘完成后回调，由它在 FileWorker 线程里发 1032 回包
//（1003 的 1004 回包是 LogicWorker 解析完立即回的，不等落盘，所以不需要 callback）
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
	short _msgid;        //来源消息 ID（1003/1031），FileWorker 据此选择处理逻辑
	int _seq;            //分块序号（从 1 开始）
	string _name;        //文件名（落盘文件名）
	int _total_size;     //文件总大小
	int _trans_size;     //当前已传输大小（进度条用）
	int _last;           //是否最后一块
	string _file_data;   //base64 编码的分块数据
	int _uid;            //上传者 uid（1031 用：拼落盘子目录 + 更新 DB/Redis；1003 填 0）
	std::function<void(const Json::Value&)> _callback; //落盘完成回调（1031 回包 1032 用，1003 为空）
};
