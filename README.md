# code

个人 C++ / Qt 练习代码汇总仓库。按学习进度归档，每个阶段一个文件夹。

本仓库**只提交源码、工程文件与资源**，编译产物与缓存已在 `.gitignore` 中全部排除。

## 进度一览

| 阶段 | 主题 | 状态 |
|---|---|---|
| [day1](day1) | 账号注册的验证码功能 —— 打通 Qt → HTTP → gRPC → SMTP 全链路 | ✅ |
| [day2](day2) | 验证码接入 Redis —— 缓存下发 + 注册时回查校验 | ✅ |
| [day3](day3) | **重置密码（忘记密码）功能** —— 校验验证码后改密，并补齐 MySQL 数据层 | ✅ |
| [day4](day4) | **基本登录** + **新增状态服务器 StatusServer** —— 登录校验密码后由状态服务分配聊天服务器并下发 token | ✅ |

---

## day1 — 账号注册的验证码功能 ✅ 已完成

`day1/` 记录了 IM 项目第一天的成果：**用户注册流程中的邮箱验证码已经能发到用户邮箱**。

### 三个模块

| 模块 | 技术栈 | 职责 |
|---|---|---|
| [`day1/CServer1`](day1/CServer1) | C++ / Boost.Beast / gRPC / protobuf | **网关服务器 GateServer**。对外提供 HTTP 接口，接收 Qt 客户端发来的请求；对内作为 gRPC 客户端，调用验证码服务 |
| [`day1/VarifyServer`](day1/VarifyServer) | Node.js / @grpc/grpc-js / nodemailer / uuid | **验证码服务**。用 `uuidv4()` 生成验证码，通过 163 SMTP 发送邮件（此时**尚未**接入 Redis） |
| [`day1/llfcchat`](day1/llfcchat) | Qt 6 / qmake / QSS | **IM 客户端**。登录界面与注册界面；注册页点击「获取验证码」后向网关发起 HTTP 请求 |

### 调用链

```
Qt 客户端(llfcchat)
      │  点击「获取验证码」
      ▼
HTTP POST /get_varifycode
      │
      ▼
CServer1 GateServer ─── LogicSystem::RegPost("/get_varifycode")
      │
      │  gRPC 同步调用（VarifyGrpcClient / message.proto）
      ▼
VarifyServer (Node.js, :50051)
      │
      ├── uuidv4() 生成验证码
      └── nodemailer 通过 163 SMTP 发送邮件
      │
      ▼
返回结果 → 客户端提示「验证码已发送」
```

### 关键文件

- **CServer1**
  - `message.proto` —— gRPC 接口定义（`GetVarifyCode` 服务与 Req/Rsp 消息体）
  - `VarifyGrpcClient.h` —— gRPC 客户端封装（Channel / Stub / ClientContext 三件套）
  - `LogicSystem.cpp` —— HTTP 路由注册，`/get_varifycode` 是 HTTP 与 gRPC 的交界处
  - `ConfigMgr.h/.cpp` —— 读取 `config.ini` 的配置管理
  - `GateServer.cpp` —— 网关入口，监听 8080
- **VarifyServer**
  - `server.js` —— gRPC 服务端入口，监听 50051
  - `email.js` —— nodemailer 发信逻辑
  - `proto.js` —— 加载 `message.proto`
  - `config.js` / `const.js` —— 读取本地配置与常量
- **llfcchat**
  - `registerdialog.ui/.cpp/.h` —— 注册界面
  - `logindialog.ui/.cpp/.h` —— 登录界面
  - `httpmgr.h/.cpp` —— HTTP 请求封装（Qt Network）
  - `style/stylesheet.qss` —— 界面样式表

---

## day2 — 验证码接入 Redis ✅ 已完成

`day2/` 在 day1 的基础上**引入了 Redis**：验证码不再"发完即弃"，而是缓存进 Redis 并设置过期时间，
客户端提交注册时由 C++ 网关回查 Redis 完成校验。**两端各自接了一层 Redis**：

- VarifyServer（Node.js）负责**写入**验证码；
- CServer1（C++）负责**读取**验证码做注册校验。

### 相比 day1 的变化

| 位置 | 变化 |
|---|---|
| **CServer1** | 新增 `RedisMgr.h/.cpp`（基于 hiredis 的连接池 + 单例封装）、`AsioIOServicePool.h/.cpp`；`config.ini` 增加 `[Redis]` 段；注册接口改为从 Redis 取验证码比对 |
| **VarifyServer** | 新增 `redis.js`（ioredis 封装）；验证码由 UUID 全串改为**取其前 4 位**，写入 Redis 并设置过期时间；若已有未过期的验证码则直接复用 |
| **llfcchat** | 注册页补齐交互，配合新的注册接口 |

### 新增文件

- `day2/CServer1/RedisMgr.h` / `RedisMgr.cpp` —— `RedisConPool` 连接池 + `RedisMgr` 单例
  对外接口：`Get` / `Set` / `Del` / `ExistsKey` / `HSet` / `HGet` / `LPush` / `LPop` / `RPush` / `RPop`
- `day2/CServer1/AsioIOServicePool.h` / `.cpp` —— Asio I/O 线程池
- `day2/VarifyServer/redis.js` —— ioredis 封装：`GetRedis` / `SetRedisExpire` / `QueryRedis`

### 验证码的完整生命周期

```
①　获取验证码

Qt 客户端 ──HTTP POST /get_varifycode──▶ CServer1 ──gRPC──▶ VarifyServer
                                                              │
                                                              ├─ GET code_<email>
                                                              │   ├─ 命中   → 复用原验证码，重发同一封邮件
                                                              │   └─ 未命中 → 生成 4 位验证码
                                                              │               SET + EXPIRE 600s
                                                              └─ nodemailer 发送 163 邮件


②　提交注册

Qt 客户端 ──HTTP POST /register──▶ CServer1 LogicSystem
                                       │
                                       └─ RedisMgr::Get("code_" + email)
                                           ├─ 取不到       → error = 1003（验证码过期）
                                           ├─ 与提交值不符 → error = 1004（验证码错误）
                                           └─ 一致         → error = 0（注册通过）
```

### 关键代码位置

- `day2/CServer1/const.h` —— `#define CODEPREFIX "code_"`，以及错误码 `VarifyExpired = 1003` / `VarifyCodeErr = 1004`
- `day2/CServer1/LogicSystem.cpp` —— 注册接口取码比对：`RedisMgr::GetInstance()->Get(CODEPREFIX + email, varify_code)`
- `day2/CServer1/RedisMgr.h` —— `RedisConPool` 连接池与 `RedisMgr` 单例
- `day2/CServer1/config.ini` —— `[Redis]` 段（Host / Port / Passwd）
- `day2/VarifyServer/server.js` —— 生成或复用验证码 + 写 Redis + 发信
- `day2/VarifyServer/redis.js` —— ioredis 封装

### 为什么把验证码放进 Redis

| 需求 | Redis 给出的答案 |
|---|---|
| 验证码到点自动失效 | `EXPIRE` 交给 Redis 托管，服务重启也不丢 |
| 用户连续点击「获取验证码」不重复发信 | 先 `GET`，命中就复用同一个验证码 |
| C++ 与 Node 两端共享同一份数据 | Redis 是跨语言的中立存储，两边都能读写 |
| 临时高频数据不写进 MySQL | 验证码"用完即弃"，天然适合内存存储 |

---

## day3 — 重置密码（忘记密码）功能 ✅ 已完成

`day3/` 在 day2 的基础上**打通了「忘记密码」流程**，同时把服务端的 **MySQL 数据层补全**。

### 本次新增

| 位置 | 新增内容 |
|---|---|
| **CServer1** | 新增 `MysqlMgr` / `MysqlDao`（MySQL 逻辑层 + 数据访问 + 连接池），`config.ini` 增加 `[Mysql]` 段；`LogicSystem` 新增 `POST /reset_pwd` 接口 |
| **VarifyServer** | 逻辑无需改动 —— 重置密码**复用**了原有的发码通路 |
| **llfcchat** | 新增 **`ResetDialog`（忘记密码界面）**；`MainWindow` 增加 `SlotSwitchReset` / `SlotSwitchLogin2` 负责界面切换；`HttpMgr` 增加 `sig_reset_mod_finish` 信号与 `RESETMOD` 模块 |

### 重置密码的完整链路

```
Qt ResetDialog（用户名 / 邮箱 / 新密码 / 验证码）
      │  ① 点「获取」→ HTTP POST /get_varifycode（复用注册那套发码逻辑）
      │  ② 点「确认」→ HTTP POST /reset_pwd
      ▼
CServer1 GateServer · LogicSystem 的 /reset_pwd 处理器（LogicSystem.cpp:149）
      │
      ├─ 关卡1　JSON 解析失败 ────────────────────→ error 1001
      ├─ 关卡2　Redis 取不到 code_<email> ────────→ error 1003（验证码过期）
      ├─ 关卡3　验证码与提交值不符 ────────────────→ error 1004（验证码错误）
      ├─ 关卡4　CheckEmail(用户名, 邮箱) 不匹配 ────→ error 1007（用户名与邮箱不匹配）
      ├─ 关卡5　UpdatePwd(用户名, 新密码) 失败 ─────→ error 1008（更新密码失败）
      └─ 全部通过 ─────────────────────────────→ error 0
```

### 与注册流程的差别

注册走的是**存储过程** `CALL reg_user(?,?,?,@result)`（一次调用完成"查重 + 发号 + 插入"）。
重置换成了两个**普通 SQL**：先 `CheckEmail` 验证"用户名与邮箱确实匹配"，再 `UpdatePwd` 改密码。
两条路都经过 `MysqlMgr → MysqlDao → 连接池` 这一套。

### 关键文件

- `CServer1/MysqlMgr.h` / `.cpp` —— MySQL 逻辑层单例（`RegUser` / `CheckEmail` / `UpdatePwd`）
- `CServer1/MysqlDao.h` / `.cpp` —— 数据访问层，内含 `MySqlPool` 连接池（5 条连接 + 每 60 秒保活探针）
- `CServer1/LogicSystem.cpp` —— `POST /reset_pwd` 的五道关卡
- `llfcchat/resetdialog.h` / `.cpp` / `.ui` —— 忘记密码界面
- `llfcchat/mainwindow.cpp` —— `SlotSwitchReset()` / `SlotSwitchLogin2()` 界面切换
- `llfcchat/httpmgr.h` —— 新增 `sig_reset_mod_finish` 信号

### 顺带修复

- `VarifyServer/redis.js` —— 去掉了 `error` 回调里的 `RedisCli.quit()`。
  原写法会在 **Redis 重启时把客户端彻底关死**（`quit()` 是优雅关闭语义，会**禁用 ioredis 的自动重连**），
  之后所有命令都报 `Connection is closed.`，**即使 Redis 已恢复也不会自愈**，只能重启进程。
  现在只打日志，并补了 `connect` / `reconnecting` 状态日志，方便一眼看出连接状态。

---

## day4 — 基本登录 + 新增状态服务器 ✅ 已完成

`day4/` 完成了**用户登录流程**，并新增了第三台服务 **StatusServer（状态服务器）**——
它是微服务架构里的"调度台"：客户端登录时，由它决定你该连哪一台聊天服务器，并给你发一张入场券（token）。

### 本次新增

| 位置 | 新增内容 |
|---|---|
| **StatusServer**（全新工程） | C++ / gRPC 状态服务器，监听 `:50052`。对外两个 gRPC 接口：`GetChatServer`（挑一台聊天服务器 + 下发 token）、`Login`（校验 token）。内部带 `RedisMgr`（含**分布式锁** `DistLock`）、`MysqlMgr` / `MysqlDao`、`ConfigMgr`、`AsioIOServicePool` |
| **CServer1** | 新增 `StatusGrpcClient.h/.cpp`（内含 `StatusConPool` 连接池 + 单例封装）；`LogicSystem` 新增 `POST /user_login` 接口；`MysqlMgr` 新增 `CheckPwd`（校验邮箱 + 密码）；`config.ini` 增加 `[StatusServer]` 段 |
| **VarifyServer** | **本次无改动** —— 登录不走验证码，与它无关（文件内容与 day3 完全一致） |
| **llfcchat** | 登录界面接通后端：`LoginDialog` 发 `POST /user_login`；`HttpMgr` 新增 `sig_login_mod_finish` 信号与 `LOGINMOD` 模块；`global.h` 新增 `ID_LOGIN_USER` / `ID_CHAT_LOGIN` 等 ReqId，以及 `ServerInfo` 结构体（存 Host / Port / Token / Uid） |

### 登录的完整链路

```
Qt LoginDialog（邮箱 + 密码）
      │  HTTP POST /user_login  { email, passwd }
      ▼
CServer1 GateServer · LogicSystem 的 /user_login 处理器（LogicSystem.cpp:221）
      │
      ├─ 关卡1　JSON 解析失败 ─────────────────────────→ error 1001
      ├─ 关卡2　MysqlMgr::CheckPwd(email, pwd) 不匹配 ─→ error 1009（密码错误）
      │
      ├─ 关卡3　StatusGrpcClient::GetChatServer(uid)
      │           │
      │           │  gRPC 同步调用（StatusConPool 取 Stub）
      │           ▼
      │      StatusServer (:50052) · StatusServiceImpl::GetChatServer
      │           ├─ getChatServer()            从 config.ini 的 [chatservers] 里挑一台聊天服务器
      │           ├─ generate_unique_string()   boost::uuids 生成 token
      │           └─ insertToken(uid, token)    SET user_token_<uid> = token（写进 Redis）
      │           │
      │           ├─ RPC 失败 ────────────────────────→ error 1002（RPC 失败）
      │           └─ RPC 成功 ────────────────────────→ 拿到 host / port / token
      │
      └─ 全部通过 ────────────────────────────────────→ error 0
                                                          + email / uid / token / host
```

### 为什么要有状态服务器

| 需求 | StatusServer 给出的答案 |
|---|---|
| 登录成功后该连哪台聊天服务器 | 网关不硬编码，改为向状态服务**问一次** |
| 客户端后续连 ChatServer 时如何证明"我登录过" | 登录成功即下发 token，写进 Redis 备查 |
| 以后有 chatserver1 / chatserver2 多台怎么办 | `config.ini` 的 `[chatservers]` 列出全部，扩展时只改配置 |
| 多台 StatusServer 可能同时发 token | `RedisMgr` 里接了 `DistLock`（基于 Redis 的分布式锁） |

**为什么不让客户端直接找 StatusServer？** 客户端只认网关一个入口（`http://localhost:8080`）。
网关对内扮演 gRPC 客户端的角色，客户端不需要知道后端有几台服务、分别在哪。

### 关键文件

- `StatusServer/StatusServiceImpl.h` / `.cpp` —— `GetChatServer` / `Login` 两个接口的实现，`ChatServer` 结构体（host / port / name / con_count）
- `StatusServer/StatusServer.cpp` —— 服务入口，注册 `StatusServiceImpl` 并监听 50052
- `StatusServer/DistLock.h` / `.cpp` —— Redis 分布式锁（`acquireLock` / `releaseLock`），由 `RedisMgr` 暴露 `Lock` / `Unlock`
- `StatusServer/config.ini` —— 除了 MySQL / Redis，还多了 `[chatservers]` 与每台聊天服务器的 `[chatserver1]` / `[chatserver2]` 段
- `CServer1/StatusGrpcClient.h` / `.cpp` —— `StatusConPool` 连接池（Stub 队列 + 条件变量）+ `StatusGrpcClient` 单例
- `CServer1/LogicSystem.cpp` —— `POST /user_login` 的三道关卡
- `CServer1/MysqlMgr.h` —— 新增 `CheckPwd(邮箱, 密码, UserInfo&)`
- `llfcchat/logindialog.cpp` —— 登录按钮的处理逻辑（`logindialog.cpp:160` 处发请求）
- `llfcchat/global.h` —— `ID_LOGIN_USER = 1004`、`ServerInfo{ Host, Port, Token, Uid }`

### 与注册 / 重置密码的差别

- **注册、重置密码**：数据校验都在 Redis 与 MySQL 里完成，走完就返回，**不涉及第二台服务**。
- **登录**：密码校验仍是 MySQL，但校验通过后**必须再跨一次服务边界**——
  由 StatusServer 决定聊天服务器和 token。这是本项目的第一个"服务间调度"场景。

### 下一步计划

- `StatusServer::getChatServer()` 目前**先返回配置里的第一台**聊天服务器；
  按连接数选最空闲那台的逻辑（从 Redis 的 `LOGIN_COUNT` 读各台连接数）已在代码里写好但处于**注释状态**，
  等 ChatServer 落地后再启用。
- `StatusServer::Login()`（校验 token）在网关侧的登录流程里还没被调用，
  它的调用方是**即将新增的 ChatServer**——客户端拿 token 连 ChatServer 时用来验证身份、并踢掉重复登录。

---

## 环境要求

| 组件 | 版本 / 说明 |
|---|---|
| Visual Studio | 2022（v143 工具集），用于编译 CServer1 与 StatusServer |
| vcpkg | 依赖：boost、protobuf、grpc、jsoncpp、hiredis |
| MySQL | Connector/C++（`mysqlcppconn`），day3 起使用；库名 `llfc`，本机实测端口 3308 |
| Qt | 6.8.x MinGW 64-bit（llfcchat） |
| Node.js | 18+，用于 VarifyServer |
| Redis | day2 起用于缓存验证码，day4 起还用于存放登录 token（本机实测端口 6380） |
| 163 邮箱 | 需开启 SMTP 并获取授权码 |

> `mysqlcppconn-9-vs14.dll` / `mysqlcppconn8-2-vs14.dll` 是 CServer1、StatusServer 运行期需要的动态库
> （合计约 25 MB），属于二进制产物，**未提交**。自己跑的话需要自备 MySQL Connector/C++，
> 并把这两个 DLL 放到可执行文件旁边。

---

## 编译 / 运行

### CServer1（网关服务器）

```bash
# 用 VS2022 打开 CServer1.sln，直接生成；或命令行：
msbuild CServer1.sln /p:Configuration=Debug /p:Platform=x64
```

运行前需保证 `config.ini` 与可执行文件在同一目录：

```ini
[GateServer]
Port=8080
[VarifyServer]
Host=127.0.0.1
Port=50051
[StatusServer]
Host=127.0.0.1
Port=50052
[Mysql]
Host=127.0.0.1
Port=3308
User=llfc
Passwd=123456
Schema=llfc
[Redis]
Host=127.0.0.1
Port=6380
Passwd=123456
```

### StatusServer（状态服务器）

```bash
# 用 VS2022 打开 StatusServer.sln，直接生成
msbuild StatusServer.sln /p:Configuration=Debug /p:Platform=x64
```

同样需要 `config.ini` 与可执行文件同目录。它比 CServer1 的配置多了聊天服务器列表：

```ini
[StatusServer]
Port=50052
Host=0.0.0.0
[chatservers]
Name=chatserver1,chatserver2
[chatserver1]
Name=chatserver1
Host=127.0.0.1
Port=8990
[chatserver2]
Name=chatserver2
Host=127.0.0.1
Port=8991
```

> day4 阶段 ChatServer 还没写，`[chatserver1]` / `[chatserver2]` 是预留位置：
> 状态服务会从中挑一台返回给客户端，端口先占着 8990 / 8991。

### VarifyServer（验证码服务）

```bash
cd day2/VarifyServer
npm install          # 依赖不在仓库里，需要自行安装
node server.js
```

**首次运行前必须创建 `config.json`**（见下方说明），并保证本机 Redis 已启动。

### llfcchat（Qt 客户端）

用 Qt Creator 打开 `llfcchat.pro`，选择 Desktop Qt 6.8.x MinGW 64-bit 套件，直接构建运行。

---

## ⚠️ 关于配置文件（重要）

### VarifyServer/config.json —— 未提交

`day*/VarifyServer/config.json` **没有提交到仓库**，因为它包含真实凭据：

- 163 邮箱账号与 **SMTP 授权码**
- Redis 连接密码

仓库里提供的是模板 [`day4/VarifyServer/config.example.json`](day4/VarifyServer/config.example.json)
（每个 `dayN/VarifyServer/` 下都放了一份）。首次使用请复制并填写自己的信息：

```bash
cd day4/VarifyServer
cp config.example.json config.json
# 然后编辑 config.json，填入自己的邮箱、授权码、Redis 密码
```

### CServer1/config.ini —— 已提交，请按需修改

`day*/CServer1/config.ini` 是可执行程序的运行期配置，随源码一起提交。里面是**本地学习环境**的端口与占位密码：

```ini
[GateServer]
Port=8080
[VarifyServer]
Host=127.0.0.1
Port=50051
[StatusServer]
Host=127.0.0.1
Port=50052
[Mysql]
Host=127.0.0.1
Port=3308
User=llfc
Passwd=123456
Schema=llfc
[Redis]
Host=127.0.0.1
Port=6380
Passwd=123456
```

### StatusServer/config.ini —— 已提交，请按需修改

`day4/StatusServer/config.ini` 同样是本地占位值，多了 `[chatservers]` 列表（见上文「编译 / 运行」）。

> 以上都是本地开发的占位值。若你往这里填了**自己的服务器地址或真实密码**，请注意本仓库是**公开仓库**，
> 提交前务必先脱敏（改成 `127.0.0.1` / `your_password` 之类）。

---

## 说明与约定

1. **不提交构建产物**：`.vs/`、`x64/`、`build/`、`node_modules/`、`moc_*`、`ui_*.h`、`*.pdb`、`*.ilk` 等全部忽略。
   （day4 四个工程的原始目录合计约 7 GB，其中绝大部分是 VS 的 `ipch` 预编译头与 `.pdb`；
   过滤后每个 day 目录只保留几 MB 的源码与资源。）
2. **protobuf 生成文件是提交的**：`message.pb.h/.cc`、`message.grpc.pb.h/.cc` 由 `protoc` 从 `message.proto` 生成。
   保留它们是为了让仓库克隆后可直接编译（Windows 上配置 protoc + grpc_cpp_plugin 较繁琐）。
   如果你更倾向"只提交 .proto"，把它们加进 `.gitignore` 并补充生成命令即可。
3. **`*.vcxproj.user`、`*.vcxproj.bak` 未提交**：前者是个人环境配置，后者是备份文件。
4. **多份 `message.proto` 必须同步**：`CServer1/message.proto`、`VarifyServer/message.proto`、
   `StatusServer/message.proto` 是各自独立的拷贝，改接口时**每一份都要改**，否则 gRPC 会出现字段不匹配。
5. **二进制依赖不提交**：`mysqlcppconn*.dll`（MySQL Connector/C++）被排除在外，需要自行准备。
