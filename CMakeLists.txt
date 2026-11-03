# Linux 迁移总控 CMake
# 用法:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build -j
#
# 依赖:
#   - Boost 1.83 (系统头文件) + libboost_filesystem
#   - gRPC / protobuf (系统)
#   - hiredis (系统)
#   - jsoncpp / mysqlcppconn (用户级前缀，见 USER_PREFIX)

cmake_minimum_required(VERSION 3.16)
project(llfc_code_linux CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 用户级依赖前缀（无 root 安装 jsoncpp/mysqlcppconn/boost_filesystem 的位置）
set(USER_PREFIX "/home/xulei/.local/prefix" CACHE PATH "user-local dependency prefix")
link_directories(${USER_PREFIX}/usr/lib/x86_64-linux-gnu)

# 让运行时能找到用户前缀里的 .so（libmysqlcppconn 等）
set(CMAKE_BUILD_RPATH "${USER_PREFIX}/usr/lib/x86_64-linux-gnu")

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)

find_library(HIREDIS_LIB hiredis REQUIRED)
find_library(JSONCPP_LIB jsoncpp
    PATHS ${USER_PREFIX}/usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH REQUIRED)
find_library(MYSQLCPPCONN_LIB mysqlcppconn
    PATHS ${USER_PREFIX}/usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

set(MYSQLCPPCONN_INCLUDE "${USER_PREFIX}/usr/include")
set(JSONCPP_INCLUDE "${USER_PREFIX}/usr/include/jsoncpp")

# 把 message.proto 生成为 pb 源文件（输出到当前二进制目录）
function(add_proto_sources target proto_dir)
    set(_out_dir ${CMAKE_CURRENT_BINARY_DIR})
    set(_gen
        ${_out_dir}/message.pb.cc      ${_out_dir}/message.pb.h
        ${_out_dir}/message.grpc.pb.cc ${_out_dir}/message.grpc.pb.h)
    add_custom_command(
        OUTPUT ${_gen}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                -I ${proto_dir}
                --cpp_out=${_out_dir}
                --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                --grpc_out=${_out_dir}
                ${proto_dir}/message.proto
        DEPENDS ${proto_dir}/message.proto
        COMMENT "protoc: ${target} <- message.proto"
        VERBATIM)
    target_sources(${target} PRIVATE ${_gen})
    target_include_directories(${target} PRIVATE ${_out_dir})
endfunction()

# 服务端公共编译配置
function(config_server target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MYSQLCPPCONN_INCLUDE}
        ${JSONCPP_INCLUDE}
        /usr/include/hiredis
        ${GRPC_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE
        Threads::Threads
        Boost::filesystem
        protobuf::libprotobuf
        ${GRPC_LIBRARIES}
        ${HIREDIS_LIB}
        ${JSONCPP_LIB}
        ${MYSQLCPPCONN_LIB})
    if(GRPC_LIBRARY_DIRS)
        target_link_directories(${target} PRIVATE ${GRPC_LIBRARY_DIRS})
    endif()
    # 把 config.ini 拷到可执行文件同级目录（ConfigMgr 从当前工作目录读取）
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/config.ini $<TARGET_FILE_DIR:${target}>)
endfunction()

add_subdirectory(GateServer)
add_subdirectory(StatusServer)
add_subdirectory(ChatServer)
add_subdirectory(ChatServer2)
