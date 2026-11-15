# GateServer —— HTTP 网关
add_executable(GateServer
    GateServer.cpp
    CServer.cpp
    HttpConnection.cpp
    LogicSystem.cpp
    MysqlDao.cpp
    MysqlMgr.cpp
    RedisMgr.cpp
    ConfigMgr.cpp
    AsioIOServicePool.cpp
    StatusGrpcClient.cpp
    VarifyGrpcClient.cpp)

add_proto_sources(GateServer ${CMAKE_CURRENT_SOURCE_DIR})
config_server(GateServer)
