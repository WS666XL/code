# ResourceServer —— 文件/资源服务器
# 只负责文件分块的接收与落盘，暂不依赖 MySQL/Redis
add_executable(ResourceServer
    ResourceServer.cpp
    CServer.cpp
    CSession.cpp
    MsgNode.cpp
    ConfigMgr.cpp
    LogicSystem.cpp
    FileSystem.cpp
    AsioIOServicePool.cpp
    base64.cpp)

config_server(ResourceServer)
