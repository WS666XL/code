# ChatServer2 —— 聊天节点 2
# 与节点 1 共用 ../ChatServer 的同一份源码，本目录只保留自己的 config.ini，
# 避免两份源码副本各自漂移（之前 icon 修复只改了 ChatServer 导致两节点行为不一致）
set(SHARED_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ChatServer)

add_executable(ChatServer2
    ${SHARED_SRC_DIR}/ChatServer.cpp
    ${SHARED_SRC_DIR}/CServer.cpp
    ${SHARED_SRC_DIR}/CSession.cpp
    ${SHARED_SRC_DIR}/ChatGrpcClient.cpp
    ${SHARED_SRC_DIR}/ChatServiceImpl.cpp
    ${SHARED_SRC_DIR}/LogicSystem.cpp
    ${SHARED_SRC_DIR}/MsgNode.cpp
    ${SHARED_SRC_DIR}/MysqlDao.cpp
    ${SHARED_SRC_DIR}/MysqlMgr.cpp
    ${SHARED_SRC_DIR}/RedisMgr.cpp
    ${SHARED_SRC_DIR}/ConfigMgr.cpp
    ${SHARED_SRC_DIR}/StatusGrpcClient.cpp
    ${SHARED_SRC_DIR}/UserMgr.cpp
    ${SHARED_SRC_DIR}/AsioIOServicePool.cpp)

add_proto_sources(ChatServer2 ${SHARED_SRC_DIR})
config_server(ChatServer2)
