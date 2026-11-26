# 轻讯（QingXun）— Qt6 + C++ 分布式即时通讯系统

一个可独立部署运行的「微信风格」分布式即时通讯系统：**Qt6 桌面客户端（`DesktopClient`） + 4 个 C++ 核心服务（含双节点 `MsgServer`） + Node.js 邮件验证码服务（`MailService`）**，
后台采用 MySQL 持久化、Redis 缓存令牌/路由与上传进度，服务间通过 gRPC 通信。

支持好友/群聊、图片/文件分块断点续传、MD5 秒传持久化、好友备注、单侧删除好友、右键删除消息、图片全屏预览与内置 AI 助手等完整能力。

---

## 1. 系统架构与服务组成

| 模块 | 作用 | 端口 |
| --- | --- | --- |
| `DesktopClient/` | Qt6 桌面客户端（`qingxun.pro`，产物 `bin/qingxun`） | — |
| `ApiGateway/` | HTTP 接入网关：获取验证码、注册、登录、重置密码 | 8080 |
| `RouteServer/` | 节点路由与调度：按在线负载分配 `MsgServer` 节点并签发登录 Token | 50052（gRPC，仅本机） |
| `MsgServer/` | 长连接消息节点（**单二进制，通过 `-c node1.ini` / `-c node2.ini` 启动双节点**） | `im-node-1`: 8090 / 50055<br>`im-node-2`: 8091 / 50056 |
| `FileServer/` | 头像、图片与文件传输服务（分块断点续传、MD5 秒传与跨节点通知） | 9090 |
| `MailService/` | Node.js + gRPC 邮箱验证码服务（发送 SMTP 邮件并写入 Redis） | 50051（gRPC，仅本机） |
| `sql/schema.sql` | MySQL 数据库初始化脚本（含建表、AI 助手账号与 `reg_user` 存储过程） | — |

```text
                        HTTP 8080                 gRPC 50051
  DesktopClient ──────▶ ApiGateway ─────────────▶ MailService（发邮件验证码）
       │                     │
       │                     │ gRPC 50052
       │              ┌──────▼───────┐
       │              │ RouteServer  │  选一台 MsgServer + 签发 Token
       │              └──────┬───────┘
       │  TCP 8090 / 8091    │
       │              ┌──────▼───────────────────┐   gRPC 50055/50056   ┌────────────┐
       └─────────────▶│ MsgServer (im-node-1 / 2)│◀────────────────────▶│ FileServer │ TCP 9090
                      └──────┬───────────────────┘                      └─────┬──────┘
                             │                                                │
                      MySQL 3306（用户/好友/消息）                     output/、static/（文件）
                      Redis 6380（token/验证码/路由/上传进度）
```

---

## 2. 核心功能

- 注册 / 登录 / 重置密码（邮箱验证码）
- 搜索用户、添加好友、好友申请与认证、好友备注、单侧删除好友、重加好友恢复历史记录
- 私聊 / 群聊（支持 Unicode emoji、Enter 发送、`Shift+Enter` 换行、日期分隔条）
- 图片消息（点击全屏预览、另存为）与文件消息（分块上传/下载**断点续传**、暂停/继续、失败重试）
- **MD5 秒传持久化**：服务端已有同内容文件直接硬链接复用，索引持久化到 `file_object` 表
- 头像裁剪上传与全网在线实时刷新
- 右键删除单条消息（单侧删除）
- 内置 AI 助手好友（`uid=900000`）：客户端直连阿里云百炼大模型，API Key 仅保存于本机

---

## 3. 云服务器一键编译与部署（Ubuntu 22.04 / 24.04）

仓库自带 [`cloud_build.sh`](cloud_build.sh)，专门适配 **1.6G~2G 小内存云服务器**（自动 `-O1 -DNDEBUG` + `-j1` + `nice -n 19`，防编译时 OOM / 卡死 SSH）：

```bash
# 1. 全新云服务器：安装依赖 + 建 4G Swap + 编译 + 写入公网 IP + 启动全部服务
sudo ./cloud_build.sh --install-deps --create-swap --public-ip <你的云服务器公网IP> --start

# 2. 已有依赖：拉取代码后直接编译（或附加 --public-ip / --start）
./cloud_build.sh --public-ip <你的云服务器公网IP> --start

# 3. 内存特别紧张时，可在编译期间临时停一下 mysql，编完自动恢复
sudo ./cloud_build.sh --pause-db --public-ip <你的云服务器公网IP> --start
```

**云服务器安全组（入方向 TCP）建议：**
- **放行**：`22`（SSH）、`8080`（ApiGateway）、`8090`、`8091`（MsgServer 双节点）、`9090`（FileServer）
- **禁止公网访问**：`3306`（MySQL）、`6380`（Redis）、`50051`、`50052`、`50055`、`50056`（内部 gRPC，默认均仅监听 `127.0.0.1`）

---

## 4. 本地开发编译与运行

### 1）安装依赖（Ubuntu / WSL2）

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config git nodejs npm \
  qt6-base-dev qt6-base-dev-tools \
  libboost-dev libboost-filesystem-dev \
  libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
  libhiredis-dev libjsoncpp-dev libmysqlcppconn-dev \
  libspdlog-dev libfmt-dev \
  mysql-server redis-server
```

### 2）初始化数据库与 Redis

```bash
sudo mysql < sql/schema.sql
```

```sql
CREATE USER IF NOT EXISTS 'xulei'@'127.0.0.1' IDENTIFIED WITH mysql_native_password BY '123456';
CREATE USER IF NOT EXISTS 'xulei'@'localhost' IDENTIFIED WITH mysql_native_password BY '123456';
GRANT ALL PRIVILEGES ON test.* TO 'xulei'@'127.0.0.1';
GRANT ALL PRIVILEGES ON test.* TO 'xulei'@'localhost';
FLUSH PRIVILEGES;
```

启动 Redis（默认配置使用 6380 端口、密码 `123456`）：

```bash
redis-server --port 6380 --requirepass 123456 --daemonize yes
redis-cli -p 6380 -a 123456 ping   # 返回 PONG
```

### 3）配置邮箱验证码（`MailService`）

```bash
cd MailService
cp config.example.json config.json   # 填入发件邮箱与 SMTP 授权码
npm install --registry=https://registry.npmmirror.com
```

### 4）编译服务端

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

> `CMakeLists.txt` 会自动检测用户前缀或系统目录，无需手动传路径参数。

编译产物：
```text
build/ApiGateway/ApiGateway
build/RouteServer/RouteServer
build/MsgServer/MsgServer          # 同目录自动复制 node1.ini / node2.ini
build/FileServer/FileServer
```

### 5）一键启停全部服务端

```bash
./start_servers.sh        # 启动 MailService + ApiGateway + RouteServer + MsgServer(node1/node2) + FileServer
./start_servers.sh stop   # 停止全部服务
```

正常端口检查输出：
```text
port 50051: OK    # MailService gRPC
port 8080:  OK    # ApiGateway HTTP
port 50052: OK    # RouteServer gRPC
port 8090:  OK    # MsgServer im-node-1 TCP
port 50055: OK    # MsgServer im-node-1 gRPC
port 8091:  OK    # MsgServer im-node-2 TCP
port 50056: OK    # MsgServer im-node-2 gRPC
port 9090:  OK    # FileServer TCP
```

### 6）编译与启动桌面客户端（`DesktopClient`）

```bash
cd DesktopClient
qmake6 qingxun.pro
make -j2
./bin/qingxun       # WSL 下中文输入法可用 ./chat-cn.sh 或 ./run_client.sh
```

连接云服务器时，修改 `DesktopClient/config.ini`（以及 `bin/config.ini`）：

```ini
[ApiGateway]
host=47.112.116.146
port=8080

[Log]
level=debug
```

---

## 5. 日志位置

统一使用 `common/Logger.h`（基于 spdlog，单文件 5MB 滚动保留 3 份，密码字段自动打码）：

| 服务 | 控制台重定向日志 | 滚动日志文件 |
| --- | --- | --- |
| `ApiGateway` | `/tmp/apigateway.log` | `build/ApiGateway/logs/apigateway.log` |
| `RouteServer` | `/tmp/routeserver.log` | `build/RouteServer/logs/routeserver.log` |
| `MsgServer (node1)` | `/tmp/msgserver_node1.log` | `build/MsgServer/logs/im-node-1.log` |
| `MsgServer (node2)` | `/tmp/msgserver_node2.log` | `build/MsgServer/logs/im-node-2.log` |
| `FileServer` | `/tmp/fileserver.log` | `build/FileServer/logs/file-node-1.log` |
| `MailService` | `/tmp/mailservice.log` | — |
| `DesktopClient` | — | `DesktopClient/bin/logs/client.log` |

---

## 6. 目录结构

```text
qingxun/
├── ApiGateway/         HTTP 接入网关（注册/登录/验证码/重置密码）
├── RouteServer/        路由调度服务（节点负载均衡与 Token 签发）
├── MsgServer/          IM 长连接消息节点（单二进制 + node1.ini / node2.ini 双节点配置）
├── FileServer/         头像/图片/文件传输服务（断点续传、MD5 秒传）
├── MailService/        Node.js gRPC 邮箱验证码服务
├── DesktopClient/      Qt6 桌面客户端（qmake 工程 qingxun.pro）
├── common/Logger.h     服务端与客户端共用统一日志组件
├── sql/schema.sql      MySQL 建表与存储过程脚本
├── bench/              端到端压测脚本与基准报告
├── CMakeLists.txt      服务端总控 CMake 构建脚本
├── cloud_build.sh      云服务器一键编译与配置脚本
├── start_servers.sh    一键启动/停止全部服务端
└── gen_compdb.sh       生成 IDE compile_commands.json
```
