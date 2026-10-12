# code — day7 分支

> **关于本仓库的组织方式**：按「一天一个分支」归档。
> 本 `day7` 分支里**只有第 7 天**的代码 —— 本次做的是**服务端**，5 个工程平铺在根目录。
> 第 1 ~ 4 天的代码在 **`master` 分支**（按 `day1/` ~ `day4/` 文件夹归档）；
> 第 5 天（4 个服务端 + Qt 客户端）在 **`day5` 分支**；第 6 天（只动了 Qt 客户端）在 **`day6` 分支**。
> 在 GitHub 页面左上角的**分支下拉框**里切换即可对比查看。

本仓库**只提交源码、工程文件与资源**，编译产物与缓存已在 `.gitignore` 中全部排除。

---

## 本次完成：服务端走向分布式

一句话：**把「一个聊天服务器」拆成了「两个平行节点」，并让登录分派、连接计数、节点间 gRPC 通道都就位。**

| # | 内容 | 说明 |
|---|---|---|
| 1 | **新增第二个聊天节点 `ChatServer2`** | 与 `ChatServer` 结构一致，靠 `config.ini` 区分身份（chatserver1 / chatserver2），各自监听自己的客户端端口与对端 gRPC 端口 |
| 2 | **StatusServer 按负载分派** | 用 Redis 哈希 `logincount` 记录每个节点的在线连接数，登录时挑**连接数最少**的节点返回给网关 |
| 3 | **节点间 gRPC 通道（骨架）** | `message.proto` 新增 `service ChatService`（5 个 RPC），每个 ChatServer 既是 gRPC **服务端**（被对端调用）也是 gRPC **客户端**（调用对端），客户端带连接池 |
| 4 | **网关补上 gRPC 登录** | `CServer1` 的 `StatusGrpcClient` 新增 `Login(uid, token)`，登录态校验改走 gRPC |

---

## 工程（本次 5 个，全是服务端）

| 工程 | 技术栈 | 职责 | 本次状态 |
|---|---|---|---|
| [`CServer1`](CServer1) | C++ / cpp-httplib / gRPC / Redis / MySQL | **HTTP 网关**。注册、登录、找回密码，转发验证码请求，把用户分派到聊天节点 | 🔧 改 3 个文件（新增 gRPC Login 调用） |
| [`StatusServer`](StatusServer) | C++ / gRPC / Redis / MySQL | **状态服务**。发 token、校验登录、**按连接数挑聊天节点** | 🆕 +2 文件，改 12 个 |
| [`ChatServer`](ChatServer) | C++ / Boost.Asio / gRPC / Redis / MySQL | **聊天节点 1**（chatserver1）。客户端 TCP 长连接 + 对端 gRPC | 🆕 +6 文件，改 18 个 |
| [`ChatServer2`](ChatServer2) | 同上 | **聊天节点 2**（chatserver2）—— **本次新建** | 🆕 整个工程 |
| [`VarifyServer`](VarifyServer) | Node.js / gRPC / nodemailer / ioredis | **验证码服务**。发邮件验证码、校验验证码 | ✅ 本次无改动 |

与 `day5` 分支相比：4 个服务端工程 → **5 个**（多了 `ChatServer2`），文件数 **34+34+25+11 → 40+40+34+27+9**。

---

## 1. 服务端拓扑

```
                        ┌──────────────────────┐
   Qt 客户端 ──HTTP────▶ │  CServer1  网关 :8080 │
                        └───────┬──────────────┘
                                │ gRPC
                    ┌───────────▼───────────┐        ┌──────────────────┐
                    │ StatusServer  :50052  │───────▶│ VarifyServer     │
                    │  · 发/校验 token      │  gRPC  │ (Node) :50051    │
                    │  · 按负载挑节点       │        └──────────────────┘
                    └───────────┬───────────┘
                                │ 返回"最少连接"的节点 host:port
        ┌───────────────────────┴───────────────────────┐
        │                                               │
┌───────▼───────────────┐                   ┌───────────▼───────────┐
│ ChatServer  (node1)   │                   │ ChatServer2 (node2)   │
│  · 客户端 TCP  :8090  │◀──gRPC 互调───────▶│  · 客户端 TCP  :8090  │
│  · 对端 gRPC :50055   │   50055 ⇄ 50056   │  · 对端 gRPC :50056   │
└───────┬───────────────┘                   └───────────┬───────────┘
        │                                               │
        └───────────────┬───────────────────────────────┘
                        │
              ┌─────────▼─────────┐   ┌──────────────┐
              │ Redis     :6380   │   │ MySQL  :3308 │
              │ · logincount 计数 │   └──────────────┘
              │ · ubaseinfo_ 缓存 │
              └───────────────────┘
```

分工很清楚：**网关只管收 HTTP，状态服务只管"你该连哪个节点"，聊天节点只管长连接和消息。**

---

## 2. 新增的分布式组件

### `message.proto` —— 节点间的协议契约

5 个工程各有一份 `message.proto`，本次在末尾新增了供**服务器之间**调用的服务：

```proto
service ChatService {
    rpc NotifyAddFriend    (AddFriendReq)    returns (AddFriendRsp)    {}
    rpc RplyAddFriend      (RplyFriendReq)   returns (RplyFriendRsp)   {}
    rpc SendChatMsg        (SendChatMsgReq)  returns (SendChatMsgRsp)  {}
    rpc NotifyAuthFriend   (AuthFriendReq)   returns (AuthFriendRsp)   {}
    rpc NotifyTextChatMsg  (TextChatMsgReq)  returns (TextChatMsgRsp)  {}
}
```

> 注意和 `StatusService` 的区别：`StatusService` 是**网关 → 状态服务**的；`ChatService` 是**聊天节点 → 聊天节点**的。
> 两个用户如果被分派到不同节点，消息就得靠 `ChatService` 转过去。

### `ChatServer` 的双重身份（`ChatServer.cpp`）

一个进程同时开两个"口"：

```cpp
// 1) 对端 gRPC 口：注册 ChatServiceImpl，监听 [SelfServer] Host:RPCPort
std::string server_address(cfg["SelfServer"]["Host"] + ":" + cfg["SelfServer"]["RPCPort"]);
ChatServiceImpl service;
builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
builder.RegisterService(&service);
std::thread grpc_server_thread([&server]() { server->Wait(); });

// 2) 启动时把自己在 Redis 里的在线数清零
RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, "0");

// 3) 客户端 TCP 口：监听 [SelfServer] Port
CServer s(io_context, atoi(port_str.c_str()));
```

### 新增文件

| 文件 | 作用 |
|---|---|
| `ChatServer/ChatServiceImpl.h/.cpp` | `ChatService` 的**服务端**实现。节点收到对端投递的请求后，从本地 `UserMgr` 找到该 uid 的 `CSession` 并推送 |
| `ChatServer/ChatGrpcClient.h/.cpp` | `ChatService` 的**客户端**。内含 `ChatConPool`（gRPC 连接池，queue + mutex + condvar），按对端节点名缓存 `Stub` |
| `ChatServer/UserMgr.h/.cpp` | `uid → shared_ptr<CSession>` 映射，加锁保护。**跨节点投递的关键**：对端发来的消息要能在本机找到接收者的连接 |
| `StatusServer/ChatGrpcClient.h/.cpp` | 状态服务侧的 `ChatService` 客户端（用于把"加好友"通知转给目标节点） |

### `ChatConPool` —— gRPC 连接池

和 `day5` 里 CServer1 连 StatusServer 的写法同源，标准三段式：

```cpp
auto stub = pool_->getConnection();          // 拿一个（池空则阻塞等）
Defer defer([&stub, this]() {
    pool_->returnConnection(std::move(stub)); // 作用域结束自动归还
});
Status status = stub->NotifyAddFriend(&context, request, &reply);
```

---

## 3. 配置说明

### 端口总表

| 服务 / 端口 | 值 | 配置位置 |
|---|---|---|
| HTTP 网关 | 8080 | `CServer1/config.ini` `[GateServer]` |
| VarifyServer gRPC | 50051 | `[VarifyServer]` |
| StatusServer gRPC | 50052 | `StatusServer/config.ini` `[StatusServer]` |
| ChatServer 客户端 TCP | 8090 | `ChatServer/config.ini` `[SelfServer] Port` |
| ChatServer 对端 gRPC | 50055 | `[SelfServer] RPCPort` |
| ChatServer2 客户端 TCP | 8090 | `ChatServer2/config.ini` `[SelfServer] Port` |
| ChatServer2 对端 gRPC | 50056 | `[SelfServer] RPCPort` |
| MySQL | 3308 | `[Mysql]` |
| Redis | 6380 | `[Redis]` |

### 节点注册表（StatusServer 视角）

`StatusServer/config.ini` 里维护"有哪些聊天节点"：

```ini
[chatservers]
Name = chatserver1,chatserver2
[chatserver1]
Name = chatserver1
Host = 127.0.0.1
Port = 8090
[chatserver2]
Name = chatserver2
Host = 127.0.0.1
Port = 8091
```

`StatusServer` 启动时把这些读进 `_servers` 映射，`getChatServer()` 遍历时对每个节点
去 Redis 查 `HGET logincount <name>`，**取连接数最小的那个**返回。

### 节点自身的身份与对端

`ChatServer/config.ini`（节点 1）：

```ini
[SelfServer]
Name=chatserver1
Host = 0.0.0.0
Port  = 8090
RPCPort=50055
[PeerServer]
Server=chatserver2
[chatserver2]
Name=chatserver2
Host=127.0.0.127
Port=50056
```

`ChatServer2/config.ini`（节点 2）是对称的镜像，`SelfServer.Name=chatserver2`、`RPCPort=50056`。

---

## 4. 相对 `day5` 的差异（逐文件 MD5 比对）

> 用 `Get-FileHash -Algorithm MD5` 把 5 个工程的每个文件和 day5 的对应文件比了一遍。

| 工程 | 新增 | 修改 | 说明 |
|---|---|---|---|
| `ChatServer` | **6** | 18 | 新增 `ChatGrpcClient.*` / `ChatServiceImpl.*` / `UserMgr.*`；`message.proto`、`const.h`、`data.h`、`config.ini`、`LogicSystem.*`、`CSession.*`、`RedisMgr.*`、`ChatServer.*` 等同步改造 |
| `ChatServer2` | **40** | — | **全新工程**（与改造后的 `ChatServer` 内容一致，仅 `config.ini` 是节点 2 的镜像） |
| `StatusServer` | **2** | 12 | 新增 `ChatGrpcClient.*`；`StatusServiceImpl.*` 改为按 `logincount` 选节点；`const.h` 新增 `USERIPPREFIX` / `USERTOKENPREFIX` / `IPCOUNTPREFIX` / `USER_BASE_INFO` / `LOGIN_COUNT` |
| `CServer1` | 0 | 3 | `StatusGrpcClient.cpp` **新增 `Login(uid, token)`**；`LogicSystem.cpp/.h` 与 day5 **代码逐行一致**（这一轮只补了中文注释） |
| `VarifyServer` | 0 | 0 | 本次无改动 |

`VarifyServer` 相对 day5 少两个文件：`config.example.json`（本分支已重新生成，见下）与 `redis123.js`（源码里已删除）。

---

## 5. 当前进度

**已经跑通的部分**

- 每个聊天节点启动时把自己在 Redis `logincount` 里的计数置 0，登录 +1、退出 -1（`LogicSystem.cpp`）
- `StatusServer` 登录时读 `logincount` 挑**连接数最少**的节点，返回 `host:port` 给网关
- 每个 `ChatServer` 独立起一个 gRPC `ChatService` 监听自己 `RPCPort`
- `CServer1` 通过 gRPC 调 `StatusServer::Login` 校验登录态

**还在骨架阶段的部分**

- `ChatGrpcClient` 构造函数里读的**段名是 `[ProtoServer]`**，而两个 `config.ini` 里写的是 `[PeerServer]`；
  同时取端口用的键是 `"Post"` 而不是 `"Port"` → 这个连接池目前**建不出来**
- `ChatServiceImpl` 的三个 `Notify*` RPC、以及 `ChatGrpcClient` 的三个同名方法目前都是**空实现**，
  跨节点投递的逻辑还没落地；`StatusServer` 侧的 `ChatGrpcClient::NotifyAddFriend` 也还没被调用
- `ChatGrpcClient::GetBaseInfo` / `ChatServiceImpl::GetBaseInfo` 在 Redis 命中分支**没有 return**
- `ChatServer2/config.ini` 的 `[SelfServer] Port` 目前也是 **8090**，和节点 1 撞端口
  （`StatusServer` 里给 chatserver2 登记的是 **8091**）；两个 `config.ini` 里对端 `Host` 都写成了 `127.0.0.127`

> 这些是复制时刻的**代码原貌**，本分支只做归档、未作任何修改，留给后续进度补齐。

---

## 环境要求

| 组件 | 版本 / 说明 |
|---|---|
| Visual Studio | 2022（MSVC v143），x64 + **Debug** 配置 |
| gRPC / protobuf | 1.34.0 / 3.13.0（各工程用 `PropertySheet1.props` 统一引依赖） |
| MySQL | 端口 3308，库名 `llfc` |
| Redis | 端口 6380 |
| Node.js | 跑 `VarifyServer` 用；`nodemailer` 发邮件 |

> `mysqlcppconn-9-vs14.dll` / `mysqlcppconn8-2-vs14.dll`（各约 15 MB / 9 MB）属于**运行时依赖**，
> 为了不把仓库撑大，这里**没有提交**，请从本机 MySQL Connector/C++ 安装目录自行拷贝到 exe 同级目录。

---

## 编译 / 运行

```bash
# 1) Redis（6380）与 MySQL（3308）先起来，并导入 llfc 库
# 2) 按顺序启动：VarifyServer → StatusServer → ChatServer → ChatServer2 → CServer1
#    每个 C++ 工程用 VS2022 打开对应的 .sln，选 x64 / Debug 生成

# VarifyServer（Node.js）
cd VarifyServer
npm install
cp config.example.json config.json   # 然后填入真实的邮箱授权码与 Redis 密码
node server.js
```

`config.ini` 会被各工程的 `QMAKE_POST_LINK` / 属性表后置事件拷到 `x64\Debug\`，
**改完 `config.ini` 要重新生成一次才生效**。

---

## ⚠️ 关于配置文件

- `VarifyServer/config.json` **含真实的 163 邮箱 SMTP 授权码与 Redis 密码，已在 `.gitignore` 中排除，不在本仓库里**。
  仓库只提供模板 `VarifyServer/config.example.json`，使用时复制成 `config.json` 再填自己的值。
  如果你的仓库历史里曾出现过 `config.json`，请到邮箱服务商后台**重新生成授权码**。
- 其余 `config.ini` 里的 `User=llfc` / `Passwd=123456` 是**本地学习环境的占位密码**，随源码提交。
- 本仓库是**公开仓库**，往配置里填真实地址或密码前请先确认已脱敏。

---

## 说明与约定

1. **不提交构建产物**：`.vs/`、`x64/`、`项目名/项目名/`（VS 嵌套中间目录）、`*.vcxproj.user`、
   `*.dll`、`*.exe`、`*.pdb`、`*.bak` 等全部忽略。
2. **protobuf 生成物保留**：`message.pb.*` / `message.grpc.pb.*` 已提交，
   这样克隆下来不必自备 protoc 与 grpc_cpp_plugin 就能编译。
3. **本分支只含服务端**。客户端（Qt）请看 `day5` / `day6` 分支。
4. 两个节点工程的源码内容基本一致，只靠 `config.ini` 区分身份 —— 这是刻意的：
   同一份代码换个配置就是另一个节点。
