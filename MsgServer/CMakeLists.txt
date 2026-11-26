# MsgServer —— IM 长连接消息节点（单二进制，通过 -c node1.ini / node2.ini 启动多实例）
add_executable(MsgServer
    MsgServer.cpp
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
    AsioIOServicePool.cpp
    DistLock.cpp
    utils.cpp)

add_proto_sources(MsgServer ${CMAKE_CURRENT_SOURCE_DIR})
config_server(MsgServer)

# 同步多节点配置文件到可执行文件同级目录
add_custom_command(TARGET MsgServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/node1.ini $<TARGET_FILE_DIR:MsgServer>/node1.ini
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/node2.ini $<TARGET_FILE_DIR:MsgServer>/node2.ini)
