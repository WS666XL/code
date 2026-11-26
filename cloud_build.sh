#!/bin/bash
# ============================================================
# 轻讯（QingXun）云服务器一键编译与配置脚本（适配 Ubuntu 22.04/24.04，兼容 1.6G~2G 小内存）
#
# 常用示例：
#   1) 仅编译（最常用，云端拉完代码直接跑）：
#        ./cloud_build.sh
#   2) 全新云服务器安装依赖 + 建 4G Swap + 编译 + 写入公网 IP + 启动：
#        sudo ./cloud_build.sh --install-deps --create-swap --public-ip 47.112.116.146 --start
#   3) 已有依赖，更新代码后重编并写入公网 IP 后重启：
#        ./cloud_build.sh --public-ip 47.112.116.146 --start
#
# 参数说明：
#   --install-deps      apt 安装 C++ / Node 编译依赖（需 root）
#   --create-swap       若无 Swap 则自动创建 4G /swapfile（需 root）
#   --pause-db          编译期间临时停用 mysql 释放内存，结束后自动恢复（需 root）
#   --public-ip <IP>    把公网 IP 写入 ApiGateway/RouteServer 配置（供外网客户端连接）
#   --jobs <N> / -j <N> 指定编译并发数（默认：内存<=2.5G 用 1，否则用 2）
#   --clean             清空 build/ 后重新编译
#   --skip-mail         跳过 MailService 的 npm install
#   --start             编译完成后立即调用 ./start_servers.sh 启动全部服务
# ============================================================
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DEPS=0
CREATE_SWAP=0
PAUSE_DB=0
PUBLIC_IP=""
JOBS=""
CLEAN=0
SKIP_MAIL=0
AUTO_START=0

while [ $# -gt 0 ]; do
    case "$1" in
        --install-deps) INSTALL_DEPS=1; shift ;;
        --create-swap)  CREATE_SWAP=1; shift ;;
        --pause-db)     PAUSE_DB=1; shift ;;
        --public-ip)    PUBLIC_IP="$2"; shift 2 ;;
        --jobs|-j)      JOBS="$2"; shift 2 ;;
        --clean)        CLEAN=1; shift ;;
        --skip-mail)    SKIP_MAIL=1; shift ;;
        --start)        AUTO_START=1; shift ;;
        -h|--help)
            sed -n '2,24p' "$0"
            exit 0
            ;;
        *)
            echo "未知参数: $1（用 --help 查看帮助）" >&2
            exit 1
            ;;
    esac
done

MEM_MB=$(awk '/MemTotal/ {printf "%d", $2/1024}' /proc/meminfo)
SWAP_MB=$(awk '/SwapTotal/ {printf "%d", $2/1024}' /proc/meminfo)
echo "[info] 本机内存: ${MEM_MB} MB, Swap: ${SWAP_MB} MB"

# 1. 可选创建 Swap（小内存云主机防止 OOM）
if [ "$CREATE_SWAP" -eq 1 ] && [ "$SWAP_MB" -lt 1024 ]; then
    echo "[1/5] 创建 4G Swap..."
    if [ ! -f /swapfile ]; then
        fallocate -l 4G /swapfile
        chmod 600 /swapfile
        mkswap /swapfile
    fi
    swapon /swapfile 2>/dev/null || true
    grep -q "/swapfile" /etc/fstab || echo "/swapfile none swap sw 0 0" >> /etc/fstab
fi

# 2. 可选安装系统依赖（Ubuntu 22.04/24.04）
if [ "$INSTALL_DEPS" -eq 1 ]; then
    echo "[2/5] 安装系统依赖..."
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y \
        build-essential cmake pkg-config git nodejs npm \
        libboost-dev libboost-filesystem-dev \
        libgrpc++-dev protobuf-compiler protobuf-compiler-grpc libprotobuf-dev \
        libhiredis-dev libjsoncpp-dev libmysqlcppconn-dev \
        libspdlog-dev libfmt-dev
fi

# 3. 按内存自动选择并发数（小内存强制 -j1，避免 Swap 抖动卡死 SSH）
if [ -z "$JOBS" ]; then
    if [ "$MEM_MB" -le 2600 ]; then
        JOBS=1
    else
        JOBS=2
    fi
fi
echo "[3/5] 编译配置: -O1 -DNDEBUG, 并发 -j${JOBS} (nice -n 19)"

# 可选编译期暂停 MySQL，结束自动恢复
MYSQL_WAS_ACTIVE=0
restore_db() {
    if [ "$MYSQL_WAS_ACTIVE" -eq 1 ]; then
        echo "[info] 恢复启动 mysql..."
        systemctl start mysql 2>/dev/null || true
    fi
}
trap restore_db EXIT

if [ "$PAUSE_DB" -eq 1 ] && systemctl is-active --quiet mysql 2>/dev/null; then
    echo "[info] 编译期间临时停止 mysql 释放内存..."
    systemctl stop mysql
    MYSQL_WAS_ACTIVE=1
fi

# 4. 安装 MailService 依赖（国内镜像加速）
if [ "$SKIP_MAIL" -eq 0 ] && [ -f "$ROOT/MailService/package.json" ]; then
    if [ ! -d "$ROOT/MailService/node_modules/@grpc/grpc-js" ]; then
        echo "[4/5] 安装 MailService (Node.js) 依赖..."
        (cd "$ROOT/MailService" && npm install --registry=https://registry.npmmirror.com --no-audit --no-fund)
    else
        echo "[4/5] MailService 依赖已存在，跳过 npm install"
    fi
fi

# 5. CMake 配置与编译 4 个 C++ 服务端（系统库模式：USER_PREFIX=""）
cd "$ROOT"
[ "$CLEAN" -eq 1 ] && rm -rf "$ROOT/build"

echo "[5/5] 编译 C++ 服务端 (ApiGateway / RouteServer / MsgServer / FileServer)..."
cmake -S "$ROOT" -B "$ROOT/build" \
    -DCMAKE_BUILD_TYPE= \
    -DCMAKE_CXX_FLAGS="-O1 -DNDEBUG" \
    -DUSER_PREFIX=""

nice -n 19 cmake --build "$ROOT/build" \
    --target ApiGateway RouteServer MsgServer FileServer \
    -j"$JOBS"

# 若指定公网 IP，自动更新配置并同步到 build/
if [ -n "$PUBLIC_IP" ]; then
    echo "[info] 写入公网 IP: ${PUBLIC_IP}"
    sed -i "/^\[FileServer\]/,/^\[/ s/^Host.*/Host = ${PUBLIC_IP}/" "$ROOT/ApiGateway/config.ini"
    sed -i "/^\[im-node-1\]/,/^\[/ s/^Host.*/Host = ${PUBLIC_IP}/" "$ROOT/RouteServer/config.ini"
    sed -i "/^\[im-node-2\]/,/^\[/ s/^Host.*/Host = ${PUBLIC_IP}/" "$ROOT/RouteServer/config.ini"
    cp "$ROOT/ApiGateway/config.ini"  "$ROOT/build/ApiGateway/config.ini"
    cp "$ROOT/RouteServer/config.ini" "$ROOT/build/RouteServer/config.ini"
fi

# 同步其余配置文件并创建 FileServer 目录
cp "$ROOT/MsgServer/node1.ini"  "$ROOT/build/MsgServer/node1.ini"
cp "$ROOT/MsgServer/node2.ini"  "$ROOT/build/MsgServer/node2.ini"
cp "$ROOT/FileServer/config.ini" "$ROOT/build/FileServer/config.ini"
mkdir -p "$ROOT/build/FileServer/output" "$ROOT/build/FileServer/static"

restore_db
MYSQL_WAS_ACTIVE=0

echo "=========================================="
echo " 编译完成！产物如下："
ls -lh \
    "$ROOT/build/ApiGateway/ApiGateway" \
    "$ROOT/build/RouteServer/RouteServer" \
    "$ROOT/build/MsgServer/MsgServer" \
    "$ROOT/build/FileServer/FileServer"
echo "=========================================="

if [ "$AUTO_START" -eq 1 ]; then
    chmod +x "$ROOT/start_servers.sh"
    "$ROOT/start_servers.sh"
else
    echo "下一步启动全部服务：./start_servers.sh"
fi
