# 轻讯（QingXun）分布式 IM 服务端总控 CMake
# 用法:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#
# 依赖:
#   - Boost (headers + libboost_filesystem)
#   - gRPC / protobuf
#   - hiredis / jsoncpp / mysqlcppconn / spdlog / fmt

cmake_minimum_required(VERSION 3.16)
project(qingxun_im CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可选用户级依赖前缀（无 root 环境自定义安装位置；系统标准安装时自动回退到 /usr）
set(USER_PREFIX "/home/xulei/.local/prefix" CACHE PATH "optional user-local dependency prefix")
if(USER_PREFIX AND EXISTS "${USER_PREFIX}/usr/lib/x86_64-linux-gnu")
    link_directories(${USER_PREFIX}/usr/lib/x86_64-linux-gnu)
    set(CMAKE_BUILD_RPATH "${USER_PREFIX}/usr/lib/x86_64-linux-gnu")
    set(_EXTRA_LIB_PATHS "${USER_PREFIX}/usr/lib/x86_64-linux-gnu")
    set(_EXTRA_INC_PATHS "${USER_PREFIX}/usr/include")
else()
    set(_EXTRA_LIB_PATHS "")
    set(_EXTRA_INC_PATHS "")
endif()

find_package(Threads REQUIRED)
find_package(Boost REQUIRED)
find_library(BOOST_FS_LIB boost_filesystem
    HINTS ${_EXTRA_LIB_PATHS} /usr/lib/x86_64-linux-gnu REQUIRED)
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)

find_library(HIREDIS_LIB hiredis
    HINTS ${_EXTRA_LIB_PATHS} /usr/lib/x86_64-linux-gnu REQUIRED)
find_library(JSONCPP_LIB jsoncpp
    HINTS ${_EXTRA_LIB_PATHS} /usr/lib/x86_64-linux-gnu REQUIRED)
find_library(MYSQLCPPCONN_LIB mysqlcppconn
    HINTS ${_EXTRA_LIB_PATHS} /usr/lib/x86_64-linux-gnu REQUIRED)
# spdlog（common/Logger.h）在 Debian/Ubuntu 上使用系统 fmt，需要显式链接
find_library(FMT_LIB fmt
    HINTS ${_EXTRA_LIB_PATHS} /usr/lib/x86_64-linux-gnu REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

find_path(JSONCPP_INCLUDE json/json.h
    HINTS ${_EXTRA_INC_PATHS}/jsoncpp /usr/include/jsoncpp /usr/include REQUIRED)
find_path(MYSQLCPPCONN_INCLUDE
    NAMES mysql_driver.h jdbc/mysql_driver.h
    HINTS ${_EXTRA_INC_PATHS} /usr/include REQUIRED)

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
        ${CMAKE_SOURCE_DIR}/common
        ${MYSQLCPPCONN_INCLUDE}
        ${JSONCPP_INCLUDE}
        /usr/include/hiredis
        ${GRPC_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE
        Threads::Threads
        Boost::headers
        ${BOOST_FS_LIB}
        protobuf::libprotobuf
        ${GRPC_LIBRARIES}
        ${HIREDIS_LIB}
        ${JSONCPP_LIB}
        ${MYSQLCPPCONN_LIB}
        ${FMT_LIB})
    if(GRPC_LIBRARY_DIRS)
        target_link_directories(${target} PRIVATE ${GRPC_LIBRARY_DIRS})
    endif()
    # 若该服务目录有 config.ini，自动拷到可执行文件同级目录
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_SOURCE_DIR}/config.ini $<TARGET_FILE_DIR:${target}>)
    endif()
endfunction()

add_subdirectory(ApiGateway)
add_subdirectory(RouteServer)
add_subdirectory(MsgServer)
add_subdirectory(FileServer)
