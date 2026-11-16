#!/bin/bash
# 一键启动本地测试所需的四个 C++ 服务（VarifyServer(Node)/Redis/MySQL 需已自行运行）
# 用法: ./start_servers.sh          前台日志写 /tmp/*.log，可用 tail -f 查看
#       ./start_servers.sh stop     停掉四个服务
# 说明: 用 setsid + nohup 双重脱离终端，关掉终端窗口服务也不会退

BUILD=/home/xulei/cpp/testllfc/build

start_one() {
    local dir=$1 bin=$2 log=$3
    cd "$BUILD/$dir" || return 1
    setsid nohup stdbuf -oL "./$bin" > "$log" 2>&1 < /dev/null &
    echo "started $bin  (log: $log)"
}

if [ "$1" = "stop" ]; then
    for n in GateServer StatusServer ChatServer ChatServer2 ResourceServer; do
        pkill -x "$n" && echo "stopped $n"
    done
    exit 0
fi

# 先清掉正在跑的旧实例（重新编译后重跑本脚本即可热更新）
for n in GateServer StatusServer ChatServer ChatServer2 ResourceServer; do
    pkill -x "$n" 2>/dev/null && echo "stopped old $n"
done
sleep 1

start_one GateServer   GateServer   /tmp/gateserver.log
start_one StatusServer StatusServer /tmp/statusserver.log
start_one ChatServer   ChatServer   /tmp/chatserver1.log
start_one ChatServer2  ChatServer2  /tmp/chatserver2.log
start_one ResourceServer ResourceServer /tmp/resourceserver.log

sleep 1
echo "---- 端口检查 ----"
for port in 8080 50052 8090 50055 8091 50056 9090; do
    (echo > /dev/tcp/127.0.0.1/$port) 2>/dev/null && echo "port $port: OK" || echo "port $port: FAIL"
done
