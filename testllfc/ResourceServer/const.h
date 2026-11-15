#pragma once
#include <string>
#include <memory>
#include <iostream>
#include <map>
#include <functional>
using namespace std;

// ===== 资源服务器专用常量 =====

//消息 ID：和客户端 FileTcpMgr 约定的一套独立编号（与 ChatServer 的 1xxx 段互不冲突）
enum MsgIds {
	ID_TEST_MSG_REQ = 1001,       //测试消息
	ID_TEST_MSG_RSP = 1002,       //测试消息回包
	ID_UPLOAD_FILE_REQ = 1003,    //上传文件请求
	ID_UPLOAD_FILE_RSP = 1004,    //上传文件回复
};

//包头布局：2 字节消息 id + 4 字节包体长度（均网络字节序/大端）
#define HEAD_TOTAL_LEN 6
#define HEAD_ID_LEN 2
#define HEAD_DATA_LEN 4

//单条消息最大长度：32KB 分块 base64 后约 44KB，放宽到 50KB
#define MAX_LENGTH  1024*50

//发送/接收队列上限（防积压打爆内存）
#define MAX_RECVQUE  2000000
#define MAX_SENDQUE  2000000

//逻辑线程池数量：按 session 哈希分发，保证同一连接的消息串行处理
#define LOGIC_WORKER_COUNT 4
//文件写入线程池数量：按文件名哈希分发，保证同一文件串行写盘
#define FILE_WORKER_COUNT 4

enum ErrorCodes {
	Success = 0,
	Error_Json = 1001,  //Json解析错误
};

// Defer类
class Defer {
public:
	Defer(std::function<void()> func) : func_(func) {}
	~Defer() { if (func_) func_(); }
private:
	std::function<void()> func_;
};
