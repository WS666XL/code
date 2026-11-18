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
	ID_SYNC_FILE_REQ = 1005,      //同步文件上传进度请求（断点续传：问服务器传到第几块了）
	ID_SYNC_FILE_RSP = 1006,      //同步文件上传进度回复
	ID_UPLOAD_HEAD_ICON_REQ = 1031, //上传头像请求（老师消息 ID 体系，与 ChatServer 的 1031 同号不同服不冲突）
	ID_UPLOAD_HEAD_ICON_RSP = 1032, //上传头像回包（最后一块落盘+DB更新后由 FileWorker 回）
	ID_DOWN_LOAD_FILE_REQ = 1033,   //下载文件请求（按 owner_uid/name 定位落盘文件，分块回 base64）
	ID_DOWN_LOAD_FILE_RSP = 1034,   //下载文件回包（data/seq/current_size/is_last/client_path）
	ID_IMG_CHAT_UPLOAD_REQ = 1037,  //上传聊天图片资源（分块，带 message_id/sender/receiver）
	ID_IMG_CHAT_UPLOAD_RSP = 1038,  //上传聊天图片资源回包（每块落盘后回一条）
};

//Redis key 前缀：与 ChatServer 保持同一套约定，
//这样 ChatServer 登录时写入的 token / 用户信息缓存，这里能用相同的 key 读到
#define USERTOKENPREFIX  "utoken_"   //登录令牌：<uid> → token（登录时 StatusServer/ChatServer 写入）
#define USER_BASE_INFO "ubaseinfo_"  //用户信息缓存：<uid> → JSON(uid/pwd/name/email/nick/desc/sex/icon)
#define USERIPPREFIX  "uip_"         //用户所在节点：<uid> → chatserver 名称（登录时写入，图片传完后按它找对方节点推通知）

//秒传（md5 去重）相关
#define FILE_MD5_PREFIX "filemd5_"       // <md5> → 已存在文件的绝对路径
#define FILE_INSTANT_PREFIX "fileinstant_" // <uid>|<md5> → 本次秒传目标名（短 TTL，拦截同一上传在途分块）

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
//下载线程池数量：按文件名哈希分发，同一文件的分块按序读取
#define DOWN_LOAD_WORKER_COUNT 4

//错误码与老师版 day38 保持同一套编号，客户端/文档对照时不易混淆
enum ErrorCodes {
	Success = 0,
	Error_Json = 1001,  //Json解析错误
	RPCFailed = 1002,      //RPC调用失败（与老师版编号一致）
	TokenInvalid = 1010,   //Token失効（与老师版错误码编号保持一致）
	UidInvalid = 1011,     //uid 无效（Redis 里查不到 token）
	FileNotExists = 1012,  //请求下载的文件不存在（与老师版编号一致）
	FileSaveRedisFailed = 1013,       //上传/下载进度写 Redis 失败
	CreateFilePathFailed = 1014,      //创建文件目录失败
	FileWritePermissionFailed = 1015, //文件打开/写入失败
	FileReadPermissionFailed = 1016,  //文件打开/读取失败
	FileSeqInvalid = 1017,            //分块序号与服务器记录不一致
	FileOffsetInvalid = 1018,         //分块偏移量越界
	FileReadFailed = 1019,            //文件读取失败（与老师版编号一致）
	RedisReadErr = 1020,              //Redis 里没有续传信息（已过期/丢失）
};

// Defer类
class Defer {
public:
	Defer(std::function<void()> func) : func_(func) {}
	~Defer() { if (func_) func_(); }
private:
	std::function<void()> func_;
};
