#!/bin/bash
# 一键启动轻讯（QingXun）全部服务端（MySQL / Redis 需已运行）
# 用法:
#   ./start_servers.sh          启动全部服务（含 MailService 若已配置）
#   ./start_servers.sh --no-mail 仅启动 C++ 服务端
#   ./start_servers.sh stop     停掉全部服务端
# 说明: 用 setsid + nohup 双重脱离终端，关掉终端窗口服务也不会退

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SCRIPT_DIR/build"

start_one() {
    local dir=$1 bin=$2 log=$3
    shift 3
    cd "$BUILD/$dir" || return 1
    setsid nohup stdbuf -oL "./$bin" "$@" > "$log" 2>&1 < /dev/null &
    echo "started $bin $*  (log: $log)"
}

stop_all() {
    for n in ApiGateway RouteServer MsgServer FileServer; do
        pkill -x "$n" 2>/dev/null && echo "stopped $n"
    done
    pkill -f "$SCRIPT_DIR/MailService/server.js" 2>/dev/null && echo "stopped MailService"
}

if [ "$1" = "stop" ]; then
    stop_all
    exit 0
fi

WITH_MAIL=1
[ "$1" = "--no-mail" ] && WITH_MAIL=0

# 先清掉正在跑的旧实例（重新编译后重跑本脚本即可热更新）
stop_all
for i in $(seq 1 50); do
    alive=0
    for n in ApiGateway RouteServer MsgServer FileServer; do
        pgrep -x "$n" > /dev/null 2>&1 && alive=1
    done
    [ "$alive" -eq 0 ] && break
    sleep 0.1
done

# 确保 FileServer 文件输出目录存在
mkdir -p "$BUILD/FileServer/output" "$BUILD/FileServer/static"

# 查找 node 可执行文件（兼容系统安装与 nvm 非交互 shell）
NODE_BIN="$(command -v node 2>/dev/null || ls -1d "$HOME"/.nvm/versions/node/*/bin/node 2>/dev/null | tail -n 1 || true)"

# 启动 MailService（Node.js 邮件验证码服务）
MAIL_STARTED=0
if [ "$WITH_MAIL" -eq 1 ] && [ -n "$NODE_BIN" ] && [ -d "$SCRIPT_DIR/MailService/node_modules" ] && [ -f "$SCRIPT_DIR/MailService/config.json" ]; then
    cd "$SCRIPT_DIR/MailService" || exit 1
    setsid nohup "$NODE_BIN" "$SCRIPT_DIR/MailService/server.js" > /tmp/mailservice.log 2>&1 < /dev/null &
    MAIL_STARTED=1
    echo "started MailService  (log: /tmp/mailservice.log)"
fi

# 启动 4 个 C++ 服务端（MsgServer 单二进制双配置起两个节点）
start_one ApiGateway  ApiGateway  /tmp/apigateway.log
start_one RouteServer RouteServer /tmp/routeserver.log
start_one MsgServer   MsgServer   /tmp/msgserver_node1.log -c node1.ini
start_one MsgServer   MsgServer   /tmp/msgserver_node2.log -c node2.ini
start_one FileServer  FileServer  /tmp/fileserver.log

sleep 1
echo "---- 端口检查 ----"
PORTS="8080 50052 8090 50055 8091 50056 9090"
[ "$MAIL_STARTED" -eq 1 ] && PORTS="50051 $PORTS"
for port in $PORTS; do
    ok=0
    for i in $(seq 1 20); do
        if (echo > /dev/tcp/127.0.0.1/$port) 2>/dev/null; then
            ok=1
            break
        fi
        sleep 0.1
    done
    [ "$ok" -eq 1 ] && echo "port $port: OK" || echo "port $port: FAIL"
done
