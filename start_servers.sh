#!/bin/bash
# 一键启动本地测试所需的四个 C++ 服务（VarifyServer(Node)/Redis/MySQL 需已自行运行）
# 用法: ./start_servers.sh          前台日志写 /tmp/*.log，可用 tail -f 查看
#       ./start_servers.sh stop     停掉四个服务
# 说明: 用 setsid + nohup 双重脱离终端，关掉终端窗口服务也不会退

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SCRIPT_DIR/build"

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
# pkill 只是发信号，旧进程可能还在优雅关闭并占着端口；
# 等它们真正退出再启动，避免新进程 bind 失败（ChatServer 会直接崩）
for i in $(seq 1 50); do
    alive=0
    for n in GateServer StatusServer ChatServer ChatServer2 ResourceServer; do
        pgrep -x "$n" > /dev/null 2>&1 && alive=1
    done
    [ "$alive" -eq 0 ] && break
    sleep 0.1
done

start_one GateServer   GateServer   /tmp/gateserver.log
start_one StatusServer StatusServer /tmp/statusserver.log
start_one ChatServer   ChatServer   /tmp/chatserver1.log
start_one ChatServer2  ChatServer2  /tmp/chatserver2.log
start_one ResourceServer ResourceServer /tmp/resourceserver.log

sleep 1
echo "---- 端口检查 ----"
for port in 8080 50052 8090 50055 8091 50056 9090; do
    ok=0
    # 服务启动有先后，最多等 2 秒再判定失败，避免误报
    for i in $(seq 1 20); do
        if (echo > /dev/tcp/127.0.0.1/$port) 2>/dev/null; then
            ok=1
            break
        fi
        sleep 0.1
    done
    [ "$ok" -eq 1 ] && echo "port $port: OK" || echo "port $port: FAIL"
done
