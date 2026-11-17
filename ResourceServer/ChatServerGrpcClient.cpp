#include "ChatServerGrpcClient.h"
#include "Logger.h"
#include "ConfigMgr.h"
#include "MysqlMgr.h"
#include "const.h"

//构造：从 config.ini 读两台 ChatServer 的 gRPC 地址建连接池。
//与 ChatServer config.ini 的 SelfServer.RPCPort 对应（chatserver1=50055 / chatserver2=50056）
ChatServerGrpcClient::ChatServerGrpcClient() {
	auto& cfg = ConfigMgr::Inst();
	for (const auto& server_name : { std::string("chatserver1"), std::string("chatserver2") }) {
		std::string host = cfg[server_name]["Host"];
		std::string port = cfg[server_name]["Port"];
		if (host.empty() || port.empty()) {
			continue;
		}
		_pools[server_name] = std::make_unique<ChatServerConPool>(host, port);
	}
}

//通知 ChatServer 把图片消息推给收图人：
//message_id 只带了消息号，消息详情（发件人/收件人/文件名）从 MySQL 补齐——
//chat_message 由 ChatServer 在 1035 时入库，这里只读不写
NotifyChatImgRsp ChatServerGrpcClient::NotifyChatImgMsg(int message_id, std::string chatserver) {
	NotifyChatImgRsp reply;
	NotifyChatImgReq request;
	request.set_message_id(message_id);

	auto find_iter = _pools.find(chatserver);
	if (find_iter == _pools.end()) {
		reply.set_error(ErrorCodes::UidInvalid);
		return reply;
	}

	auto chat_msg = MysqlMgr::GetInstance()->GetChatMsgById(message_id);
	if (chat_msg == nullptr) {
		reply.set_error(ErrorCodes::FileNotExists);
		return reply;
	}

	request.set_file_name(chat_msg->content);
	request.set_from_uid(chat_msg->sender_id);
	request.set_to_uid(chat_msg->recv_id);
	request.set_thread_id(chat_msg->thread_id);
	//消息类型透传（1=图片 3=文件）：收图人客户端按它决定画图片气泡还是文件卡片
	request.set_msg_type(chat_msg->msg_type);

	//文件消息的 content 编码为 "unique_name|原始文件名"（| 分隔，uuid 不含 |）。
	//file_name 只传 unique_name（落盘/下载定位用），原始文件名单独透传给文件卡片显示
	std::string unique_name = chat_msg->content;
	std::string original_name;
	auto sep = unique_name.find('|');
	if (sep != std::string::npos) {
		original_name = unique_name.substr(sep + 1);
		unique_name = unique_name.substr(0, sep);
	}
	request.set_file_name(unique_name);
	request.set_original_name(original_name);

	//图片总大小从落盘文件取（通知里带上，客户端下载时显示占位尺寸用）
	auto file_dir = ConfigMgr::Inst().GetFileOutPath();
	auto uid_str = std::to_string(chat_msg->sender_id);
	auto file_path = (file_dir / uid_str / unique_name);
	boost::system::error_code ec;
	auto file_size = boost::filesystem::file_size(file_path, ec);
	if (!ec) {
		request.set_total_size(static_cast<int64_t>(file_size));
	}

	auto& pool = find_iter->second;
	auto stub = pool->getConnection();
	if (stub == nullptr) {
		reply.set_error(ErrorCodes::UidInvalid);
		return reply;
	}

	grpc::ClientContext context;
	grpc::Status status = stub->NotifyChatImgMsg(&context, request, &reply);
	Defer defer([&stub, &pool]() {
		pool->returnConnection(std::move(stub));
		});

	if (!status.ok()) {
		reply.set_error(ErrorCodes::UidInvalid);
	}
	return reply;
}

//头像变更广播：某用户上传新头像后，遍历所有 ChatServer 节点发 NotifyAvatarUpdate，
//各节点把 1057 通知推给自己节点上的全部在线 session（谁关心谁处理）。
//不按 uip_<uid> 查节点——要通知的是"所有人"，逐个查反而要遍历 Redis 全量 key
void ChatServerGrpcClient::NotifyAvatarUpdate(int uid, const std::string& icon) {
	for (auto& pair : _pools) {
		auto stub = pair.second->getConnection();
		if (stub == nullptr) {
			continue;
		}
		NotifyAvatarUpdateReq request;
		request.set_uid(uid);
		//新头像文件名随通知下发：客户端本地的 icon 可能还是默认编号/资源路径，
		//必须知道服务器上的新文件名才能删缓存重新下载
		request.set_icon(icon);
		NotifyAvatarUpdateRsp reply;
		grpc::ClientContext context;
		grpc::Status status = stub->NotifyAvatarUpdate(&context, request, &reply);
		Defer defer([&stub, &pair]() {
			pair.second->returnConnection(std::move(stub));
			});
		if (!status.ok()) {
			LLOG_ERROR << "NotifyAvatarUpdate to " << pair.first << " failed: "
				<< status.error_message() << std::endl;
		}
	}
}
