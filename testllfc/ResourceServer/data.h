#pragma once
#include <string>
#include <memory>
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

//一次分块上传任务：从 1003 请求里解析出来的字段 + 解码前的 base64 数据
struct FileTask {
	FileTask(shared_ptr<CSession> session, string name,
		int seq, int total_size, int trans_size, int last,
		string file_data) :_session(session),
		_seq(seq), _name(name), _total_size(total_size),
		_trans_size(trans_size), _last(last), _file_data(file_data)
	{}
	shared_ptr<CSession> _session;  //回执要发回给这个连接
	int _seq;            //分块序号（从 1 开始）
	string _name;        //文件名（落盘文件名）
	int _total_size;     //文件总大小
	int _trans_size;     //当前已传输大小（进度条用）
	int _last;           //是否最后一块
	string _file_data;   //base64 编码的分块数据
};
