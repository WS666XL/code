# FileServer —— 头像、图片与文件传输服务（支持断点续传、MD5 秒传与跨节点通知）
add_executable(FileServer
    FileServer.cpp
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

add_proto_sources(FileServer ${CMAKE_CURRENT_SOURCE_DIR})
config_server(FileServer)
