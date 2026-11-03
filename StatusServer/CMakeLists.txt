# StatusServer —— 状态服务
add_executable(StatusServer
    StatusServer.cpp
    StatusServiceImpl.cpp
    ChatGrpcClient.cpp
    MysqlDao.cpp
    MysqlMgr.cpp
    RedisMgr.cpp
    ConfigMgr.cpp)

add_proto_sources(StatusServer ${CMAKE_CURRENT_SOURCE_DIR})
config_server(StatusServer)
