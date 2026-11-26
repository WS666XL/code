# ApiGateway —— HTTP 接入网关（注册/登录/验证码/重置密码）
add_executable(ApiGateway
    ApiGateway.cpp
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

add_proto_sources(ApiGateway ${CMAKE_CURRENT_SOURCE_DIR})
config_server(ApiGateway)
