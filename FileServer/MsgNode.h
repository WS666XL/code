#pragma once
#include <string>
#include "Logger.h"
#include "const.h"
#include <iostream>
#include <boost/asio.hpp>
using namespace std;
using boost::asio::ip::tcp;
class LogicSystem;
class MsgNode
{
public:
	MsgNode(int max_len) :_total_len(max_len), _cur_len(0) {
		_data = new char[_total_len + 1]();
		_data[_total_len] = '\0';
	}

	~MsgNode() {
		LLOG_INFO << "destruct MsgNode" << endl;
		delete[] _data;
	}

	void Clear() {
		::memset(_data, 0, _total_len);
		_cur_len = 0;
	}

	int _cur_len;    // 当前已经存了多少有效字节
	int _total_len;  // 缓冲区总最大容量（能存放的最大字节数），文件分块较大所以用 int
	char* _data;     // 缓冲区内存指针，存放收到的二进制报文数据
};

class RecvNode :public MsgNode {
	friend class LogicSystem;
	friend class LogicWorker;  //task_callback 要读 _msg_id
public:
	RecvNode(int max_len, short msg_id);
private:
	short _msg_id;
};

class SendNode :public MsgNode {
	friend class LogicSystem;
public:
	//总长度 = 包体 + 6 字节头部（2B id + 4B len），头部在此处组装
	SendNode(const char* msg, int max_len, short msg_id);
private:
	short _msg_id;
};
