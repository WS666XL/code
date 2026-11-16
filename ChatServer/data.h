#pragma once
#include <string>
#include <vector>
#include <memory>
#include "const.h"
struct UserInfo {//用户信息
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

struct ApplyInfo {//好友申请信息
	ApplyInfo(int uid, std::string name, std::string desc,
		std::string icon, std::string nick, int sex, int status)
		:_uid(uid), _name(name), _desc(desc),
		_icon(icon), _nick(nick), _sex(sex), _status(status) {
	}

	int _uid;
	std::string _name;
	std::string _desc;
	std::string _icon;
	std::string _nick;
	int _sex;
	int _status;
};

//聊天线程信息（对应 chat_thread + private_chat 两张表的一条查询结果）
struct ChatThreadInfo {
	int _thread_id;
	std::string _type;     // "private" or "group"
	int _user1_id;    //私聊时对应 private_chat.user1_id，群聊时设为 0
	int _user2_id;    //私聊时对应 private_chat.user2_id，群聊时设为 0
	std::string _group_name;  //群聊时对应 group_chat.group_name（私聊为空）——1026 回包带给客户端建群条目
	int _member_count = 0;    //群聊成员数（私聊为 0）——客户端条目显示"群名(N)"用
};

//一条聊天消息（对应 chat_message 表）
struct ChatMessage {
	int message_id;
	int thread_id;
	int sender_id;
	int recv_id;
	std::string unique_id;  //客户端生成的消息唯一标识，用于消息回执匹配
	std::string content;
	std::string chat_time;  //消息时间字符串
	int status;             //0=未读 1=已读 2=撤回
	int msg_type;           //对应 ChatMsgType
};

//分页查询结果：多取一条用于判断是否还有下一页
struct PageResult {
	std::vector<ChatMessage> messages;
	bool load_more;
	int next_cursor;  //本页最后一条 message_id，作为下页查询起点
};