#pragma once
#include <functional>

// AI 好友固定 uid（客户端 global.h 有同名常量，两端必须一致）
constexpr int AI_BOT_UID = 900000;

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
	CREATE_CHAT_FAILED = 1012, //创建聊天失败
	LOAD_CHAT_FAILED = 1013, //加载聊天失败
	DELETE_CHAT_MSG_FAILED = 1014, //删除单条消息失败
	UPDATE_USER_INFO_FAILED = 1015, //修改个人资料失败
	UPDATE_FRIEND_FAILED = 1016,    //修改好友备注失败
	DELETE_FRIEND_FAILED = 1017,    //删除好友失败
};

//消息类型：和 chat_message 表的 msg_type 字段对应
enum class ChatMsgType {
	TEXT = 0,  //文本消息
	PIC = 1,   //图片消息
	FILE = 3,  //文件消息（与客户端 ChatMsgType::FILE 同号；day41 文件聊天复用图片的传输链路）
};

//会话形式：和客户端 ChatFormType 同号（1017 请求的 form_type 字段区分私聊/群聊）
enum class ChatFormType {
	PRIVATE = 0,
	GROUP = 1,
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
	ID_LOAD_CHAT_THREAD_REQ = 1025, //加载聊天线程请求
	ID_LOAD_CHAT_THREAD_RSP = 1026, //加载聊天线程回复
	ID_CREATE_PRIVATE_CHAT_REQ = 1027, //创建私聊请求
	ID_CREATE_PRIVATE_CHAT_RSP = 1028, //创建私聊回复
	ID_LOAD_CHAT_MSG_REQ = 1029,      //加载聊天消息请求
	ID_LOAD_CHAT_MSG_RSP = 1030,      //加载聊天消息回复
	ID_IMG_CHAT_MSG_REQ = 1035,       //图片聊天消息请求（先入库拿 message_id，文件随后走 1037 传资源服务器）
	ID_IMG_CHAT_MSG_RSP = 1036,       //图片聊天消息回复（带入库后的 message_id）
	ID_NOTIFY_IMG_CHAT_MSG_REQ = 1039, //通知用户图片聊天信息（资源服务器落盘完成后经 gRPC 触发转发）
	ID_CREATE_GROUP_REQ = 1055,   //建群请求（发起人选好友成群，老师体系未用 1050+ 段）
	ID_CREATE_GROUP_RSP = 1056,   //建群回复（带回 thread_id 和群名）
	ID_NOTIFY_AVATAR_UPDATE_REQ = 1057, //头像变更通知（服务端推在线用户，客户端删缓存重新下载）
	ID_DELETE_CHAT_MSG_REQ = 1061, //删除单条消息请求（只删当前用户视角，落 chat_message_deleted 表）
	ID_DELETE_CHAT_MSG_RSP = 1062, //删除单条消息回复
	ID_SET_FRIEND_REMARK_REQ = 1063, //设置好友备注请求（写 friend.back）
	ID_SET_FRIEND_REMARK_RSP = 1064, //设置好友备注回复
	ID_DELETE_FRIEND_REQ = 1065,     //删除好友请求（只删自己这一侧）
	ID_DELETE_FRIEND_RSP = 1066,     //删除好友回复
	ID_UPDATE_USER_INFO_REQ = 1020,  //修改个人资料请求（昵称/描述）
	ID_UPDATE_USER_INFO_RSP = 1022,  //修改个人资料回复
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

