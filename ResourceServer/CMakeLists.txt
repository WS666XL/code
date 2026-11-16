# ResourceServer —— 文件/资源服务器
# 文件分块的接收与落盘；1031 头像/1037 聊天图片上传后还会连 MySQL/Redis，
# 并通过 gRPC（NotifyChatImgMsg）通知对方所在的 ChatServer 转发图片消息
add_executable(ResourceServer
    ResourceServer.cpp
    CServer.cpp
    CSession.cpp
    MsgNode.cpp
    ConfigMgr.cpp
    LogicSystem.cpp
    FileSystem.cpp
    RedisMgr.cpp
    MysqlDao.cpp
    ChatServerGrpcClient.cpp
    AsioIOServicePool.cpp
    base64.cpp)

# 生成 message.pb（NotifyChatImgReq/Rsp 等，proto 从 ChatServer 同步过来）
add_proto_sources(ResourceServer ${CMAKE_CURRENT_SOURCE_DIR})

config_server(ResourceServer)
