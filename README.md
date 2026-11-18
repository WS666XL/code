# llfc 即时通讯项目（Qt6 + C++ 分布式服务 · Linux/WSL 版）

一个能真正跑起来的「微信风格」聊天项目：Qt6 桌面客户端 + 5 个 C++ 服务 + 1 个 Node 验证码服务，
后台用 MySQL 存数据、Redis 存登录 token 和上传进度，服务之间用 gRPC 通信。

本项目是在课程 `llfcchat`（day5 ~ day41）基础上继续自研补全的版本，
额外做了文件/图片断点续传、好友备注、单侧删除好友、右键删除消息、图片预览等能力。

> 这篇 README 面向第一次接触这个项目的新手：
> **不需要先看懂所有代码，按顺序照做就能把项目跑起来。**
> 遇到问题先看最后的「常见问题」。

---

## 目录

- [1. 项目组成](#1-项目组成)
- [2. 功能清单](#2-功能清单)
- [3. 环境准备](#3-环境准备)
- [4. 第一步：初始化数据库](#4-第一步初始化数据库)
- [5. 第二步：启动 MySQL 和 Redis](#5-第二步启动-mysql-和-redis)
- [6. 第三步：修改配置文件](#6-第三步修改配置文件)
- [7. 第四步：编译服务端](#7-第四步编译服务端)
- [8. 第五步：编译客户端](#8-第五步编译客户端)
- [9. 第六步：按顺序启动](#9-第六步按顺序启动)
- [10. 第七步：注册两个账号开始聊天](#10-第七步注册两个账号开始聊天)
- [11. 日志（排查问题先看这里）](#11-日志排查问题先看这里)
- [12. 常见问题](#12-常见问题)
- [13. 消息协议速查（想改代码再看）](#13-消息协议速查想改代码再看)
- [14. 目录结构](#14-目录结构)
- [15. 分支说明](#15-分支说明)

---

## 1. 项目组成

| 模块 | 作用 | 端口 |
| --- | --- | --- |
| `testqt/` | Qt6 桌面客户端（聊天界面、联系人、设置） | — |
| `GateServer/` | HTTP 网关：获取验证码、注册、登录、重置密码 | 8080 |
| `StatusServer/` | 登录后分配一台 ChatServer，并发放登录 token | 50052 |
| `ChatServer/` | 聊天主服务：好友、私聊/群聊、历史消息、在线转发 | 8090（TCP）/ 50055（gRPC） |
| `ChatServer2/` | 第二台聊天节点，代码与 ChatServer 相同，只是配置不同 | 8091（TCP）/ 50056（gRPC） |
| `ResourceServer/` | 头像、图片、文件的上传/下载、断点续传 | 9090 |
| `VarifyServer/` | Node.js + gRPC：发送邮箱验证码，验证码写 Redis | 50051 |
| `sql/schema.sql` | 数据库建表脚本（含注册用存储过程） | — |

一句话理解各服务的关系：

```text
                        HTTP 8080                 gRPC 50051
  Qt 客户端 ──────────▶ GateServer ─────────────▶ VarifyServer（发邮件验证码）
      │                     │
      │                     │ gRPC 50052
      │              ┌──────▼───────┐
      │              │ StatusServer │  选一台 ChatServer + 发 token
      │              └──────┬───────┘
      │  TCP 8090 / 8091    │
      │              ┌──────▼──────────┐   gRPC 50055/50056   ┌────────────────┐
      └─────────────▶│ ChatServer 1 / 2│◀────────────────────▶│ ResourceServer │ TCP 9090
                     └──────┬──────────┘                      └───────┬────────┘
                            │                                         │
                     MySQL 3306（用户/好友/消息）              output/、static/（文件）
                     Redis 6380（token/验证码/上传进度）
```

> 新手只需记住：**客户端先走 HTTP 登录，拿到 ChatServer 地址和 token 后，再走 TCP 聊天。**

---

## 2. 功能清单

- 注册 / 登录 / 重置密码（邮箱验证码）
- 搜索用户、添加好友、好友申请与认证
- 私聊、群聊（建群时从好友里选人）
- 文本消息（支持 Unicode emoji）、图片消息、文件消息
- 图片点击预览、另存为
- 文件上传/下载**断点续传**、暂停/继续
- 文件下载保存到系统「下载/llfcchat/原始文件名」，支持另存为、打开所在文件夹、上传失败重试
- 相同文件二次发送走 **md5 秒传**：服务器已有就直接硬链接，不重复上传、不重复占磁盘
- 秒传索引持久化到 MySQL `file_object` 表，Redis 重启也不会重复存文件
- 聊天列表按「今天 / 昨天 / 更早」显示日期分隔条
- 群聊消息气泡显示发送者名字
- 头像上传，在线用户头像实时刷新
- 好友备注（备注优先显示）、单侧删除好友、重新加好友恢复历史记录
- AI 助手好友：每个用户自动拥有、不可删除；客户端直连百炼大模型，API Key 只存本机，聊天不落库
- 右键删除单条消息（只影响自己，不删对方记录）

---

## 3. 环境准备

以 **Ubuntu 22.04/24.04（WSL2 也可以）** 为例，先安装编译工具和依赖：

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config git \
  qt6-base-dev qt6-base-dev-tools \
  libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
  libhiredis-dev libboost-all-dev \
  libjsoncpp-dev libmysqlcppconn-dev \
  mysql-server redis-server
```

还需要：

- **Node.js 18+**（`node -v` 查看；版本太低可用 nvm 安装）
- **MySQL 8.x**、**Redis 6+**
- WSL 下运行 Qt 图形界面需要 **WSLg**（Windows 11 自带）或自己配置 X Server

> 本项目 CMake 默认把 `jsoncpp`、`mysqlcppconn`、`boost_filesystem` 当作
> **用户级依赖** 放在 `/home/xulei/.local/prefix` 下。
> 如果你用上面的 `apt` 安装到系统目录，编译时加 `-DUSER_PREFIX=/` 即可（见第 7 步）。

---

## 4. 第一步：初始化数据库

项目根目录执行：

```bash
mysql -uroot -p < sql/schema.sql
# Ubuntu 默认 root 使用 auth_socket，也可以这样导入：
# sudo mysql < sql/schema.sql
```

会创建：

- 数据库 `test` 和全部表（空表）
- 注册用存储过程 `reg_user`（**注册接口依赖它，不能漏**）
- `user_id` 初始值 1000，之后注册的用户从 uid 1001 开始分配

然后创建一个供程序连接的数据库账号（和配置文件里的账号保持一致）：

```sql
CREATE USER IF NOT EXISTS 'llfc'@'%' IDENTIFIED BY '123456';
CREATE USER IF NOT EXISTS 'llfc'@'localhost' IDENTIFIED BY '123456';
GRANT ALL PRIVILEGES ON test.* TO 'llfc'@'%';
GRANT ALL PRIVILEGES ON test.* TO 'llfc'@'localhost';
FLUSH PRIVILEGES;
```

> 不想用 `llfc/123456` 也可以，但要同步修改下面所有 `config.ini` 里的 `[Mysql]`。

> AI 助手：初始化脚本会创建一个固定用户 `AI助手`（uid=900000）。
> 客户端首次给 AI 发消息时会弹窗要求填写百炼 API Key，
> Key 只保存在本机 `AppData/llfcchat/ai.ini`，不会进代码或仓库；
> 也可以在 AI 聊天页右上角「...」里随时修改。

---

## 5. 第二步：启动 MySQL 和 Redis

```bash
sudo service mysql start

# 项目的配置文件默认 Redis 端口是 6380、密码 123456
redis-server --port 6380 --requirepass 123456 --daemonize yes

# 检查
redis-cli -p 6380 -a 123456 ping     # 返回 PONG 就对了
```

> 如果你的 Redis 跑在默认 6379 且没有密码，
> 请把所有 `config.ini` 里的 `[Redis]` 改成实际端口/密码。

---

## 6. 第三步：修改配置文件

需要确认这几个文件（都在各自模块目录下）：

| 文件 | 要改什么 |
| --- | --- |
| `GateServer/config.ini` | MySQL、Redis 地址/账号/密码 |
| `StatusServer/config.ini` | MySQL、Redis，以及两台 ChatServer 的地址 |
| `ChatServer/config.ini` | MySQL、Redis、自己的 TCP/gRPC 端口、对端 ChatServer |
| `ChatServer2/config.ini` | 同上（端口改成 8091 / 50056） |
| `ResourceServer/config.ini` | MySQL、Redis、文件输出目录、两台 ChatServer 的 gRPC 地址 |
| `testqt/config.ini` | 客户端的 GateServer 地址（`host` / `port`） |
| `VarifyServer/config.json` | 邮箱 SMTP 授权码、MySQL、Redis |

`VarifyServer/config.json` 没有现成模板时，复制示例再填：

```bash
cd VarifyServer
cp config.example.json config.json
```

`email.user` 填发件邮箱（示例是 163 邮箱），`email.pass` 填 **SMTP 授权码**（不是登录密码）。
发件邮箱需要提前开启 SMTP 服务。

---

## 7. 第四步：编译服务端

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

依赖装在系统目录时用：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DUSER_PREFIX=/
cmake --build build -j$(nproc)
```

编译产物：

```text
build/GateServer/GateServer
build/StatusServer/StatusServer
build/ChatServer/ChatServer
build/ChatServer2/ChatServer2
build/ResourceServer/ResourceServer
```

> 改了代码后重新执行 `cmake --build build -j$(nproc)` 即可，不用重新 cmake。

---

## 8. 第五步：编译客户端

```bash
cd testqt
qmake6 llfcchat.pro
make -j$(nproc)
```

生成的可执行文件在 `testqt/bin/llfcchat`，
`config.ini` 会自动复制到 `bin/` 目录（客户端从可执行文件所在目录读取配置）。

启动：

```bash
cd testqt
./bin/llfcchat
```

想模拟两个人聊天：**把这个程序再启动一次**，登录另一个账号即可。

---

## 9. 第六步：按顺序启动

### 1）启动验证码服务（Node）

```bash
cd VarifyServer
npm install
node server.js
```

看到 gRPC 监听 `0.0.0.0:50051` 就成功了。

### 2）启动 5 个 C++ 服务

另开一个终端，在项目根目录执行：

```bash
./start_servers.sh
```

脚本会依次启动 GateServer、StatusServer、ChatServer、ChatServer2、ResourceServer，
日志输出到 `/tmp/gateserver.log`、`/tmp/chatserver1.log` 等。

停止全部服务：

```bash
./start_servers.sh stop
```

脚本最后会做端口检查，正常应看到：

```text
port 8080: OK     # GateServer HTTP
port 50052: OK    # StatusServer gRPC
port 8090: OK     # ChatServer1 TCP
port 50055: OK    # ChatServer1 gRPC
port 8091: OK     # ChatServer2 TCP
port 50056: OK    # ChatServer2 gRPC
port 9090: OK     # ResourceServer TCP
```

### 3）启动客户端

```bash
cd testqt
./bin/llfcchat
```

---

## 10. 第七步：注册两个账号开始聊天

1. 客户端点「注册」，填邮箱 → 获取验证码 → 去邮箱收验证码 → 完成注册
2. 用账号 A 登录，再启动一个客户端用账号 B 登录
3. A 在搜索框搜 B 的用户名 → 添加好友
4. B 在「联系人 → 新的朋友」里同意申请
5. 双方即可私聊，也可以发图片、文件、建群

> 本地开发如果没配好邮箱，也可以直接看 `VarifyServer` 的控制台输出，
> 或从 Redis 里查验证码（验证码 key 带项目前缀）。

---

## 11. 日志（排查问题先看这里）

项目不再用 `std::cout` / `qDebug` 打日志，统一走 `common/Logger.h`（基于 spdlog）：

- 同时输出到**控制台**和**文件**
- 单文件 5MB，最多保留 3 个备份
- 每条日志都立即落盘，进程崩溃也能看到最后几条
- 线程安全，密码类配置自动打码

日志位置：

| 程序 | 日志文件 |
| --- | --- |
| GateServer | `build/GateServer/logs/gateserver.log` |
| StatusServer | `build/StatusServer/logs/statusserver.log` |
| ChatServer | `build/ChatServer/logs/chatserver1.log` |
| ChatServer2 | `build/ChatServer2/logs/chatserver2.log` |
| ResourceServer | `build/ResourceServer/logs/reserver.log` |
| 客户端 | `testqt/bin/logs/client.log` |

格式：

```text
[2026-11-17 16:42:19.026] [info] [21534] [GateServer.cpp:149] Server started on port 8080
 时间                        级别    线程id   文件:行号            内容
```

改日志级别：

- 服务端：各自的 `config.ini`

```ini
[Log]
Level=info      # trace / debug / info / warn / error
Path=logs
```

- 客户端：`testqt/config.ini`

```ini
[Log]
level=debug
```

在代码里写日志：

```cpp
#include "Logger.h"

LLOG_DEBUG << "recv msg id " << msg_id;
LLOG_INFO  << "user login success, uid=" << uid;
LLOG_WARN  << "redis reconnect: " << err;
LLOG_ERROR << "mysql error: " << e.what();
```

> 写法和原来的 `cout` / `qDebug` 一样，直接接 `<<` 即可。

---

## 12. 常见问题

### 编译时找不到 jsoncpp / mysqlcppconn

CMake 默认在 `/home/xulei/.local/prefix` 找这两个库。
依赖装在系统目录时，重新 cmake 并加参数：

```bash
cmake -S . -B build -DUSER_PREFIX=/
```

### 注册失败 / 提示数据库错误

确认 `sql/schema.sql` 已经完整执行，尤其是存储过程 `reg_user`：

```sql
SHOW PROCEDURE STATUS WHERE Db = 'test';
```

没有的话单独重新导入 `sql/schema.sql`（注意：脚本会重建表，已有数据会清空）。

### ChatServer2 启动后立刻退出，日志有 `terminate called without an active exception`

这是启动时的偶发问题（两台 ChatServer 同时向 StatusServer 注册时出现）。
直接单独再启动一次即可：

```bash
cd build/ChatServer2 && ./ChatServer2
```

或者重新执行 `./start_servers.sh`。

### Qt 客户端报 `xcb-cursor0` / 平台插件加载失败

```bash
sudo apt install -y libxcb-cursor0
```

WSL 需要 WSLg（Windows 11）或配置好的 X Server，否则窗口显示不出来。

### emoji 显示成方块

```bash
sudo apt install -y fonts-noto-color-emoji
fc-cache -f
```

### 图片/文件发不出去，或对方下载失败

- 确认 `ResourceServer` 正在运行（9090 端口）
- 文件实际保存在 `build/ResourceServer/output/`，静态资源在 `build/ResourceServer/static/`
- 看 `/tmp/resourceserver.log` 和 `/tmp/chatserver1.log` 有没有报错

### 端口被占用

```bash
ss -lntp | grep -E "8080|8090|8091|9090"
./start_servers.sh stop
```

---

## 13. 消息协议速查（想改代码再看）

客户端与 ChatServer 之间是 **自定义 TCP 协议**：

```text
4 字节包头（网络字节序） + JSON 包体
┌────────────┬────────────┬─────────────────────┐
│ msg_id 2B  │ length 2B  │ JSON（length 字节）  │
└────────────┴────────────┴─────────────────────┘
```

ResourceServer 的文件协议是 **6 字节包头**（2 字节消息 ID + 4 字节长度）。

常见消息 ID：

| ID | 含义 |
| --- | --- |
| 1005 / 1006 | 登录聊天服务器 / 回包 |
| 1007 / 1008 | 搜索用户 / 回包 |
| 1009 ~ 1015 | 好友申请、认证及通知 |
| 1017 / 1018 / 1019 | 发送文本消息 / 回执 / 对方消息通知 |
| 1025 / 1026 | 拉取会话列表 / 回包 |
| 1027 / 1028 | 创建私聊 / 回包 |
| 1029 / 1030 | 拉取历史消息 / 回包 |
| 1035 / 1036 / 1039 | 图片消息入库 / 回执 / 收图通知 |
| 1037 / 1038 | 图片、文件上传到 ResourceServer |
| 1055 / 1056 | 建群 / 回包 |
| 1061 / 1062 | 删除单条消息 / 回包 |
| 1063 / 1064 | 设置好友备注 / 回包 |
| 1065 / 1066 | 删除好友 / 回包 |

新增一个功能时一般改 4 个地方：

1. `testqt/global.h`：加请求/回包 ID
2. `ChatServer/LogicSystem.cpp`：在 `_fun_callbacks` 注册服务端处理函数
3. `testqt/tcpmgr.cpp`：加发送函数和回包 handler
4. 客户端对应页面：连接信号、刷新界面

---

## 14. 目录结构

```text
testllfc/
├── ChatServer/         聊天主服务（消息、好友、群聊、历史记录）
├── ChatServer2/        第二个聊天节点（只放配置，复用 ChatServer 源码）
├── GateServer/         HTTP 网关（注册/登录/验证码/重置密码）
├── StatusServer/       聊天节点分配 + token 发放
├── ResourceServer/     头像/图片/文件上传下载、断点续传
├── VarifyServer/       Node.js gRPC 邮箱验证码服务
├── testqt/             Qt6 客户端（qmake 工程 llfcchat.pro）
├── common/Logger.h     服务端/客户端共用的统一日志
├── sql/schema.sql      数据库初始化脚本
├── CMakeLists.txt      服务端总构建脚本
├── start_servers.sh    一键启动/停止 5 个 C++ 服务
├── testfile/           独立的小测试程序（可忽略）
└── llfcchat/           老师原版参考代码（已被 .gitignore 忽略，不在仓库里）
```

---

## 15. 分支说明

- `test22`：当前最新自研版本（好友备注/删除好友、删除消息、图片预览等）
- `test21`：文件/图片断点续传版本
- `test20` 及更早：按课程进度提交的历史快照
- `master` / `day5` ~ `day10`：课程早期阶段的快照

学习时建议直接看 `test22`；想对比老师的实现，可以在本地放置 `llfcchat/` 目录后按课程章节对照。

---

本项目仅用于学习交流。
