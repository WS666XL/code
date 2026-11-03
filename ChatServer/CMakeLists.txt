# ChatServer —— 聊天节点 1
add_executable(ChatServer
    ChatServer.cpp
    CServer.cpp
    CSession.cpp
    ChatGrpcClient.cpp
    ChatServiceImpl.cpp
    LogicSystem.cpp
    MsgNode.cpp
    MysqlDao.cpp
    MysqlMgr.cpp
    RedisMgr.cpp
    ConfigMgr.cpp
    StatusGrpcClient.cpp
    UserMgr.cpp
    AsioIOServicePool.cpp)

add_proto_sources(ChatServer ${CMAKE_CURRENT_SOURCE_DIR})
config_server(ChatServer)
