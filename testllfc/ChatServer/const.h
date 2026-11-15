#pragma once
#include <functional>


enum ErrorCodes {
	Success = 0,
	Error_Json = 1001,  //Json解析错误
	RPCFailed = 1002,  //RPC请求错误
	VarifyExpired = 1003, //验证码过期
	VarifyCodeErr = 1004, //验证码错误
	UserExist = 1005,       //用户已经存在
	PasswdErr = 1006,    //密码错误
	EmailNotMatch = 1007,  //邮箱不匹配
	PasswdUpFailed = 1008,  //更新密码失败
	PasswdInvalid = 1009,   //密码更新失败
	TokenInvalid = 1010,   //Token失效
	UidInvalid = 1011,  //uid无效
};


// Defer类
class Defer {
public:
	// 接受一个lambda表达式或者函数指针
	Defer(std::function<void()> func) : func_(func) {}

	// 析构函数中执行传入的函数
	~Defer() {
		func_();
	}

private:
	std::function<void()> func_;
};

#define MAX_LENGTH  1024*2
//头部总长度
#define HEAD_TOTAL_LEN 4
//头部id长度
#define HEAD_ID_LEN 2
//头部数据长度
#define HEAD_DATA_LEN 2
#define MAX_RECVQUE  10000
#define MAX_SENDQUE 1000


enum MSG_IDS {
	MSG_CHAT_LOGIN = 1005, //用户登陆
	MSG_CHAT_LOGIN_RSP = 1006, //用户登陆回包
	ID_SEARCH_USER_REQ = 1007, //用户搜索请求
	ID_SEARCH_USER_RSP = 1008, //搜索用户回包
	ID_ADD_FRIEND_REQ = 1009, //申请添加好友请求
	ID_ADD_FRIEND_RSP = 1010, //申请添加好友回复
	ID_NOTIFY_ADD_FRIEND_REQ = 1011,  //通知用户添加好友申请
	ID_AUTH_FRIEND_REQ = 1013,  //认证好友请求
	ID_AUTH_FRIEND_RSP = 1014,  //认证好友回复
	ID_NOTIFY_AUTH_FRIEND_REQ = 1015, //通知用户认证好友申请
	ID_TEXT_CHAT_MSG_REQ = 1017, //文本聊天信息请求
	ID_TEXT_CHAT_MSG_RSP = 1018, //文本聊天信息回复
	ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019, //通知用户文本聊天信息
	ID_NOTIFY_OFF_LINE_REQ = 1021, //通知用户下线
	ID_HEART_BEAT_REQ = 1023,      //心跳请求
	ID_HEARTBEAT_RSP = 1024,       //心跳回复
};
// 用户 IP 前缀，用于拼接用户 IP 对应的 Redis 键，例如：uip_1001
#define USERIPPREFIX  "uip_"

// 分布式锁 key 前缀：拼上 uid 后作为登录互斥锁的 Redis 键，例如 lock_1001
#define LOCK_PREFIX "lock_"

// 用户会话前缀：记录 uid 当前绑定的 session id，例如 usession_1001
#define USER_SESSION_PREFIX "usession_"

// 分布式锁的持有时长（秒）：超时自动释放，防止持锁进程崩溃造成死锁
#define LOCK_TIME_OUT 10

// 抢锁的最长等待时长（秒）：超过仍未抢到则放弃本次登录互斥
#define ACQUIRE_TIME_OUT 5

// 用户 Token 前缀，用于拼接用户登录令牌对应的 Redis 键，例如：utoken_1001
#define USERTOKENPREFIX  "utoken_"

// IP 访问次数前缀，用于拼接 IP 对应的访问计数 Redis 键，例如：ipcount_127.0.0.1
#define IPCOUNTPREFIX  "ipcount_"

// 用户基础信息前缀，用于拼接用户基础信息对应的 Redis 键，例如：ubaseinfo_1001
#define USER_BASE_INFO "ubaseinfo_"

// 登录总次数对应的 Redis 键，用于记录登录次数
#define LOGIN_COUNT    "logincount"

// 用户名信息前缀，用于拼接用户名对应的 Redis 键，例如：nameinfo_zhangsan
#define NAME_INFO      "nameinfo_"

