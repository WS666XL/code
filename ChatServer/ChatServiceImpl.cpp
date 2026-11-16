#include "ChatServiceImpl.h"
#include "UserMgr.h"
#include "CSession.h"
#include "CServer.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "RedisMgr.h"
#include "MysqlMgr.h"

ChatServiceImpl::ChatServiceImpl()
{

}

Status ChatServiceImpl::NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* reply)
{
	//查找用户是否在本服务器
	auto touid = request->touid();
	auto session = UserMgr::GetInstance()->GetSession(touid);

	Defer defer([request, reply]() {
		reply->set_error(ErrorCodes::Success);
		reply->set_applyuid(request->applyuid());
		reply->set_touid(request->touid());
		});

	//用户不在内存中则直接返回
	if (session == nullptr) {
		return Status::OK;
	}

	//在内存中则直接发送通知对方
	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["applyuid"] = request->applyuid();
	rtvalue["name"] = request->name();
	rtvalue["desc"] = request->desc();
	rtvalue["icon"] = request->icon();
	rtvalue["sex"] = request->sex();
	rtvalue["nick"] = request->nick();

	std::string return_str = rtvalue.toStyledString();

	session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
	return Status::OK;
}

Status ChatServiceImpl::NotifyAuthFriend(ServerContext* context, const AuthFriendReq* request,
	AuthFriendRsp* reply) {
	
	//查找用户是否在本服务器
	auto touid = request->touid();
	auto fromuid = request->fromuid();
	auto session = UserMgr::GetInstance()->GetSession(touid);

	Defer defer([request, reply]() {
		reply->set_error(ErrorCodes::Success);
		reply->set_fromuid(request->fromuid());
		reply->set_touid(request->touid());
		});

	//用户不在内存中则直接返回
	if (session == nullptr) {
		return Status::OK;
	}

	//在内存中则直接发送通知对方
	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["fromuid"] = request->fromuid();
	rtvalue["touid"] = request->touid();

	std::string base_key = USER_BASE_INFO + std::to_string(fromuid);
	auto user_info = std::make_shared<UserInfo>();
	bool b_info = GetBaseInfo(base_key, fromuid, user_info);
	if (b_info) {
		rtvalue["name"] = user_info->name;
		rtvalue["nick"] = user_info->nick;
		rtvalue["icon"] = user_info->icon;
		rtvalue["sex"] = user_info->sex;
	}
	else {
		rtvalue["error"] = ErrorCodes::UidInvalid;
	}

	std::string return_str = rtvalue.toStyledString();

	session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
	return Status::OK;
}

Status ChatServiceImpl::NotifyTextChatMsg(::grpc::ServerContext* context,
	const TextChatMsgReq* request, TextChatMsgRsp* reply) {
	//查找用户是否在本服务器
	auto touid = request->touid();
	auto session = UserMgr::GetInstance()->GetSession(touid);
	reply->set_error(ErrorCodes::Success);

	//用户不在内存中则直接返回
	if (session == nullptr) {
		return Status::OK;
	}

	//在内存中则直接发送通知对方
	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["fromuid"] = request->fromuid();
	rtvalue["touid"] = request->touid();
	rtvalue["thread_id"] = request->thread_id();

	//将聊天数据组织为数组（消息已落库，这里带上自增 message_id 和时间）
	Json::Value text_array;
	for (auto& msg : request->textmsgs()) {
		Json::Value element;
		element["content"] = msg.msgcontent();
		element["unique_id"] = msg.unique_id();
		element["message_id"] = msg.msg_id();
		element["chat_time"] = msg.chat_time();
		text_array.append(element);
	}
	rtvalue["chat_datas"] = text_array;

	std::string return_str = rtvalue.toStyledString();

	session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
	return Status::OK;
}

// 资源服务器在聊天图片完整落盘后调过来：把图片消息通知转发给本节点的收图人。
// 客户端收到 1039 后建占位气泡，并按 img_name 向资源服务器下载图片
Status ChatServiceImpl::NotifyChatImgMsg(::grpc::ServerContext* context,
	const NotifyChatImgReq* request, NotifyChatImgRsp* reply) {
	//查找收图人是否在本服务器
	auto touid = request->to_uid();
	auto session = UserMgr::GetInstance()->GetSession(touid);
	reply->set_error(ErrorCodes::Success);

	//用户不在内存中（离线/在另一节点）则直接返回，消息已落库，登录后走历史记录加载
	if (session == nullptr) {
		return Status::OK;
	}

	//组织 1039 通知（字段名与老师版客户端解析保持一致）
	Json::Value rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["message_id"] = request->message_id();
	rtvalue["sender_id"] = request->from_uid();
	rtvalue["receiver_id"] = request->to_uid();
	rtvalue["img_name"] = request->file_name();
	rtvalue["total_size"] = std::to_string(request->total_size());
	rtvalue["thread_id"] = request->thread_id();
	//消息类型透传（1=图片 3=文件），客户端按它画对应气泡
	rtvalue["msg_type"] = request->msg_type();
	//原始文件名透传（文件消息卡片显示用）
	rtvalue["original_name"] = request->original_name();

	std::string return_str = rtvalue.toStyledString();
	session->Send(return_str, ID_NOTIFY_IMG_CHAT_MSG_REQ);
	return Status::OK;
}

//头像变更在线推送：把 1057 通知推给本节点全部在线用户。
//通知只有 uid 一个业务字段——客户端自己判断本地有没有这个人的头像缓存，
//有才删文件重新下载，没有直接忽略（谁关心谁处理，服务端不做过滤）
Status ChatServiceImpl::NotifyAvatarUpdate(::grpc::ServerContext* context,
	const NotifyAvatarUpdateReq* request, NotifyAvatarUpdateRsp* reply) {
	auto uid = request->uid();
	reply->set_error(ErrorCodes::Success);
	reply->set_uid(uid);

	Json::Value rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["uid"] = uid;
	//新头像文件名透传：客户端靠它更新本地缓存的 icon 并发起重新下载。
	//只给 uid 不够——接收方本地 icon 可能还是默认编号，无法拼出下载路径
	rtvalue["icon"] = request->icon();
	std::string return_str = rtvalue.toStyledString();

	//锁内拷贝的快照：Send 内部有自己的锁，可以在快照上慢慢发
	auto sessions = UserMgr::GetInstance()->GetOnlineSessions();
	for (auto& session : sessions) {
		session->Send(return_str, ID_NOTIFY_AVATAR_UPDATE_REQ);
	}
	std::cout << "avatar update notify pushed to " << sessions.size()
		<< " online sessions, uid=" << uid << std::endl;
	return Status::OK;
}


bool ChatServiceImpl::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
	//优先查redis中查询用户信息
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		userinfo->uid = root["uid"].asInt();
		userinfo->name = root["name"].asString();
		userinfo->pwd = root["pwd"].asString();
		userinfo->email = root["email"].asString();
		userinfo->nick = root["nick"].asString();
		userinfo->desc = root["desc"].asString();
		userinfo->sex = root["sex"].asInt();
		userinfo->icon = root["icon"].asString();
		std::cout << "user login uid is  " << userinfo->uid << " name  is "
			<< userinfo->name << " pwd is " << userinfo->pwd << " email is " << userinfo->email << endl;
	}
	else {
		//redis中没有则查询mysql
		//查询数据库
		std::shared_ptr<UserInfo> user_info = nullptr;
		user_info = MysqlMgr::GetInstance()->GetUser(uid);
		if (user_info == nullptr) {
			return false;
		}

		userinfo = user_info;

		//将数据库内容写入redis缓存
		Json::Value redis_root;
		redis_root["uid"] = uid;
		redis_root["pwd"] = userinfo->pwd;
		redis_root["name"] = userinfo->name;
		redis_root["email"] = userinfo->email;
		redis_root["nick"] = userinfo->nick;
		redis_root["desc"] = userinfo->desc;
		redis_root["sex"] = userinfo->sex;
		redis_root["icon"] = userinfo->icon;
		RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	}
	return true;
}

// 跨服踢人的 gRPC 入口：目标 uid 的旧连接在本节点上，发 1021 并清除该 session
Status ChatServiceImpl::NotifyKickUser(::grpc::ServerContext* context,
	const KickUserReq* request, KickUserRsp* reply)
{
	//查找用户是否在本服务器
	auto uid = request->uid();
	auto session = UserMgr::GetInstance()->GetSession(uid);
	std::cout << "NotifyKickUser rpc received, uid=" << uid << " session="
		<< (session ? "found" : "null") << std::endl;

	Defer defer([request, reply]() {
		reply->set_error(ErrorCodes::Success);
		reply->set_uid(request->uid());
		});

	//用户不在内存中则直接返回
	if (session == nullptr) {
		return Status::OK;
	}

	//在内存中则先发下线通知，再清除旧的连接
	session->NotifyOffline(uid);
	//清除旧的连接
	_p_server->ClearSession(session->GetSessionId());

	return Status::OK;
}

// main 里注入 CServer 指针
void ChatServiceImpl::RegisterServer(std::shared_ptr<CServer> pServer)
{
	_p_server = pServer;
}