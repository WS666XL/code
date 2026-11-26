# RouteServer —— 节点路由与登录令牌签发服务
add_executable(RouteServer
    RouteServer.cpp
    StatusServiceImpl.cpp
    ChatGrpcClient.cpp
    MysqlDao.cpp
    MysqlMgr.cpp
    RedisMgr.cpp
    ConfigMgr.cpp)

add_proto_sources(RouteServer ${CMAKE_CURRENT_SOURCE_DIR})
config_server(RouteServer)
