# ResourceServer —— 文件/资源服务器
# 文件分块的接收与落盘；1031 头像上传还会连 MySQL/Redis（更新 user.icon + 校验 token）
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
    AsioIOServicePool.cpp
    base64.cpp)

config_server(ResourceServer)
