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
	ID_UPLOAD_HEAD_ICON_REQ = 1031, //上传头像请求（老师消息 ID 体系，与 ChatServer 的 1031 同号不同服不冲突）
	ID_UPLOAD_HEAD_ICON_RSP = 1032, //上传头像回包（最后一块落盘+DB更新后由 FileWorker 回）
	ID_DOWN_LOAD_FILE_REQ = 1033,   //下载文件请求（按 owner_uid/name 定位落盘文件，分块回 base64）
	ID_DOWN_LOAD_FILE_RSP = 1034,   //下载文件回包（data/seq/current_size/is_last/client_path）
};

//Redis key 前缀：与 ChatServer 保持同一套约定，
//这样 ChatServer 登录时写入的 token / 用户信息缓存，这里能用相同的 key 读到
#define USERTOKENPREFIX  "utoken_"   //登录令牌：<uid> → token（登录时 StatusServer/ChatServer 写入）
#define USER_BASE_INFO "ubaseinfo_"  //用户信息缓存：<uid> → JSON(uid/pwd/name/email/nick/desc/sex/icon)

//包头布局：2 字节消息 id + 4 字节包体长度（均网络字节序/大端）
#define HEAD_TOTAL_LEN 6
#define HEAD_ID_LEN 2
#define HEAD_DATA_LEN 4

//单条消息最大长度：32KB 分块 base64 后约 44KB，放宽到 50KB
#define MAX_LENGTH  1024*50

//上传/下载的分块大小：与客户端 global.h 的 MAX_FILE_LEN 保持一致（32KB）
#define FILE_CHUNK_LEN (1024*32)

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
	TokenInvalid = 1010,   //Token失効（与老师版错误码编号保持一致）
	UidInvalid = 1011,     //uid 无效（Redis 里查不到 token）
	FileNotExists = 1012,  //请求下载的文件不存在（与老师版编号一致）
	FileWritePermissionFailed = 1015, //文件打开/写入失败
	FileReadFailed = 1016,            //文件读取失败（与老师版编号一致）
};

// Defer类
class Defer {
public:
	Defer(std::function<void()> func) : func_(func) {}
	~Defer() { if (func_) func_(); }
private:
	std::function<void()> func_;
};
