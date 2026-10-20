#pragma once
#include <string>
#include "const.h"
#include <iostream>
#include <boost/asio.hpp>
using namespace std;
using boost::asio::ip::tcp;
class LogicSystem;
class MsgNode
{
public:
	MsgNode(short max_len) :_total_len(max_len), _cur_len(0) {
		_data = new char[_total_len + 1]();
		_data[_total_len] = '\0';
	}

	~MsgNode() {
		std::cout << "destruct MsgNode" << endl;
		delete[] _data;
	}

	void Clear() {
		::memset(_data, 0, _total_len);
		_cur_len = 0;
	}

	short _cur_len;    // 当前已经存了多少有效字节
	short _total_len;  // 缓冲区总最大容量（能存放的最大字节数）
	char* _data;       // 缓冲区内存指针，存放收到的二进制报文数据
};

class RecvNode :public MsgNode {
	friend class LogicSystem;
public:
	RecvNode(short max_len, short msg_id);
private:
	short _msg_id;
};

class SendNode:public MsgNode {
	friend class LogicSystem;
public:
	SendNode(const char* msg,short max_len, short msg_id);
private:
	short _msg_id;
};

