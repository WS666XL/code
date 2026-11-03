#!/bin/bash
# 恋恋风辰 IM 系统一键启停（Linux 迁移版）
# 用法: ./run_all.sh start|stop|status

BASE=/home/xulei/cpp
BUILD=$BASE/build
NODE_BIN=$HOME/.local/opt/node-v20.18.1-linux-x64/bin
LOGDIR=$BASE/logs
mkdir -p "$LOGDIR"

# 干净环境（终端里 IDE 注入的 LD_LIBRARY_PATH 会导致 GLIBCXX 报错）
CLEAN_ENV=(env -u LD_LIBRARY_PATH)

start_one() {  # $1=名字  $2=工作目录  $3=命令
    if pgrep -x "$1" >/dev/null || ( [ "$1" = VarifyServer ] && pgrep -f 'node server.js' >/dev/null ); then
        echo "[跳过] $1 已在运行"
        return
    fi
    ( cd "$2" && "${CLEAN_ENV[@]}" $3 > "$LOGDIR/$1.log" 2>&1 & )
    echo "[启动] $1 (日志: logs/$1.log)"
}

stop_one() {  # $1=进程名
    pkill -x "$1" 2>/dev/null && echo "[停止] $1" || echo "[跳过] $1 未运行"
}

case "$1" in
start)
    # 0) 用户级 Redis 6380（系统重启后需要重新拉起）
    if ! redis-cli -p 6380 -a 123456 --no-auth-warning ping 2>/dev/null | grep -q PONG; then
        redis-server --port 6380 --requirepass 123456 --daemonize yes \
            --dir "$HOME/.local/redis-6380" --save '' --appendonly no
        echo "[启动] Redis 6380"
    else
        echo "[跳过] Redis 6380 已在运行"
    fi
    # 1) 按依赖顺序启动
    start_one VarifyServer "$BASE/VarifyServer" "PATH=$NODE_BIN:\$PATH node server.js"
    start_one StatusServer "$BUILD/StatusServer" ./StatusServer
    start_one ChatServer   "$BUILD/ChatServer"   ./ChatServer
    start_one ChatServer2  "$BUILD/ChatServer2"  ./ChatServer2
    start_one GateServer   "$BUILD/GateServer"   ./GateServer
    sleep 1
    "$0" status
    ;;
stop)
    stop_one GateServer
    stop_one ChatServer2
    stop_one ChatServer
    stop_one StatusServer
    pkill -f 'node server.js' 2>/dev/null && echo "[停止] VarifyServer"
    ;;
status)
    echo "--- 端口监听 ---"
    ss -tln | awk 'NR>1 {print $4}' | grep -E ':(8080|8090|8091|50051|50052|50055|50056|6380)$' | sort
    echo "--- 进程 ---"
    pgrep -x GateServer >/dev/null    && echo "GateServer    运行中" || echo "GateServer    未运行"
    pgrep -x StatusServer >/dev/null  && echo "StatusServer  运行中" || echo "StatusServer  未运行"
    pgrep -x ChatServer >/dev/null    && echo "ChatServer    运行中" || echo "ChatServer    未运行"
    pgrep -x ChatServer2 >/dev/null   && echo "ChatServer2   运行中" || echo "ChatServer2   未运行"
    pgrep -f 'node server.js' >/dev/null && echo "VarifyServer  运行中" || echo "VarifyServer  未运行"
    ;;
*)
    echo "用法: $0 start|stop|status"
    ;;
esac
