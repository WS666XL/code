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
};
// 用户 IP 前缀，用于拼接用户 IP 对应的 Redis 键，例如：uip_1001
#define USERIPPREFIX  "uip_"

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

