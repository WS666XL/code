# code

个人 C++ / Qt 练习代码汇总仓库。按学习进度归档，每个阶段一个文件夹。

本仓库**只提交源码、工程文件与资源**，编译产物与缓存已在 `.gitignore` 中全部排除。

## 进度一览

| 阶段 | 主题 | 状态 |
|---|---|---|
| [day1](day1) | 账号注册的验证码功能 —— 打通 Qt → HTTP → gRPC → SMTP 全链路 | ✅ |
| [day2](day2) | 验证码接入 Redis —— 缓存下发 + 注册时回查校验 | ✅ |

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

## 环境要求

| 组件 | 版本 / 说明 |
|---|---|
| Visual Studio | 2022（v143 工具集），用于编译 CServer1 |
| vcpkg | 依赖：boost、protobuf、grpc、jsoncpp、hiredis |
| Qt | 6.8.x MinGW 64-bit（llfcchat） |
| Node.js | 18+，用于 VarifyServer |
| Redis | day2 起使用，用于缓存验证码（本机实测端口 6380） |
| 163 邮箱 | 需开启 SMTP 并获取授权码 |

> CServer1 目前**尚未接入 MySQL**，用户表查询在代码中是注释状态。

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
[Redis]
Host=127.0.0.1
Port=6380
Passwd=123456
```

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

仓库里提供的是模板 [`day2/VarifyServer/config.example.json`](day2/VarifyServer/config.example.json)。首次使用请复制并填写自己的信息：

```bash
cd day2/VarifyServer
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
[Redis]
Host=127.0.0.1
Passwd=123456
Port=6380
```

> 上面是本地开发的占位值。若你往这里填了**自己的服务器地址或真实密码**，请注意本仓库是**公开仓库**，
> 提交前务必先脱敏（改成 `127.0.0.1` / `your_password` 之类）。

---

## 说明与约定

1. **不提交构建产物**：`.vs/`、`x64/`、`build/`、`node_modules/`、`moc_*`、`ui_*.h`、`*.pdb`、`*.ilk` 等全部忽略。
   （原始工程目录含约 3 GB 的 VS 缓存，本仓库每个 day 目录只保留约 1 MB 的源码。）
2. **protobuf 生成文件是提交的**：`message.pb.h/.cc`、`message.grpc.pb.h/.cc` 由 `protoc` 从 `message.proto` 生成。
   保留它们是为了让仓库克隆后可直接编译（Windows 上配置 protoc + grpc_cpp_plugin 较繁琐）。
   如果你更倾向"只提交 .proto"，把它们加进 `.gitignore` 并补充生成命令即可。
3. **`*.vcxproj.user`、`*.vcxproj.bak` 未提交**：前者是个人环境配置，后者是备份文件。
4. **两份 `message.proto` 必须同步**：`CServer1/message.proto` 与 `VarifyServer/message.proto` 是各自独立的拷贝，
   改接口时两边都要改，否则 gRPC 会出现字段不匹配。
