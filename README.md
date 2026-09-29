# code — day5 分支

> **关于本仓库的组织方式**：按「一天一个分支」归档。
> 本 `day5` 分支里**只有第 5 天**的代码，5 个工程平铺在根目录；
> 第 1 ~ 4 天的代码在 **`master` 分支**上（按 `day1/` ~ `day4/` 文件夹归档）。
> 在 GitHub 页面左上角的**分支下拉框**里切换 `master` / `day5` 即可对比查看。

本仓库**只提交源码、工程文件与资源**，编译产物与缓存已在 `.gitignore` 中全部排除。

---

## 本次完成

| # | 内容 | 说明 |
|---|---|---|
| 1 | **服务端完成了 token 的验证** | 新增 `ChatServer`（长连接聊天服务器）。客户端登录时从状态服务器拿到的 token，连上 ChatServer 后要再验证一次，验证通过才允许进入聊天 |
| 2 | **客户端基本的界面** | 新增聊天主界面 `ChatDialog`（左侧会话/联系人列表 + 右侧聊天区）、`TcpMgr`（TCP 长连接 + 自定义协议拆包）、`UserMgr` 等，登录成功后自动切到聊天界面 |

---

## 五个工程

| 工程 | 技术栈 | 职责 | 本次状态 |
|---|---|---|---|
| [`ChatServer`](ChatServer) | C++ / Boost.Asio / gRPC / hiredis / MySQL | **聊天服务器（新）**。维护客户端 TCP 长连接，收登录消息后向 `StatusServer` 校验 token | 🆕 全新 |
| [`CServer1`](CServer1) | C++ / Boost.Beast / gRPC | **网关服务器 GateServer**。对外 HTTP（`:8080`），转发验证码/注册/重置密码/登录 | 🔧 仅改 `LogicSystem.cpp` |
| [`StatusServer`](StatusServer) | C++ / gRPC / hiredis / MySQL | **状态服务器**。为登录用户挑选聊天服务器并下发 token，同时提供 token 校验接口 | 🔧 改动 16 个文件 |
| [`VarifyServer`](VarifyServer) | Node.js / grpc-js / nodemailer / ioredis | **验证码服务**（`:50051`） | ✅ 无改动 |
| [`llfcchat`](llfcchat) | Qt 6 / qmake / QSS | **IM 客户端**。登录/注册/找回密码 + 本次新增的聊天界面与 TCP 链路 | 🆕 新增 18 个文件 |

---

## 1. 服务端：token 验证闭环

### 新增 ChatServer

`ChatServer` 是继 CServer1（HTTP 网关）、VarifyServer（验证码）、StatusServer（状态）之后的第四台服务。
它监听 TCP 长连接（本机 `:8090`，见 `config.ini` 的 `[SelfServer]`），用**定长头 + JSON 体**的自定义协议收消息。

| 文件 | 作用 |
|---|---|
| `ChatServer.cpp` / `CServer.h/.cpp` | Asio 的 acceptor，接受 TCP 连接、创建 `CSession` |
| `CSession.h/.cpp` | 一条客户端连接的会话对象：收发队列、粘包拆包、断线清理 |
| `MsgNode.h/.cpp` | 收发消息节点（`_msg_id` + `_total_len` + `_data`） |
| `LogicSystem.h/.cpp` | 按 `msg_id` 分发到处理函数；本日的核心是 `LoginHandler` |
| `data.h` | `UserInfo`（uid / name / email / pwd） |
| `StatusGrpcClient.h/.cpp` | 作为 gRPC 客户端调用 `StatusServer`（`GetChatServer` / `Login`），内含 `StatusConPool` 连接池 |
| `MysqlMgr` / `MysqlDao` | 校验通过后按 uid 查用户信息（`SELECT * FROM user WHERE uid = ?`） |
| `RedisMgr` / `const.h` | Redis 封装、错误码与协议常量 |

### 协议：4 字节头 + JSON 体

```
#define HEAD_TOTAL_LEN 4      // 头总长
#define HEAD_ID_LEN    2      // 前 2 字节：消息 id（大端）
#define HEAD_DATA_LEN  2      // 后 2 字节：正文长度（大端）
#define MAX_LENGTH     1024*2 // 单包正文上限
```

消息 id（服务端 `const.h` 与客户端 `global.h` 必须一致）：

| id | 含义 |
|---|---|
| `1005` | `MSG_CHAT_LOGIN` 客户端请求登录聊天服务器 |
| `1006` | `MSG_CHAT_LOGIN_RSP` 服务端回包 |

### token 验证发生在哪

`ChatServer/LogicSystem.cpp` 的 `LoginHandler`（约 74 行起）：

```cpp
auto uid   = root["uid"].asInt();
auto token = root["token"].asString();
// 从状态服务器获取token匹配是否准确
auto rsp = StatusGrpcClient::GetInstance()->Login(uid, token);
rtvalue["error"] = rsp.error();
if (rsp.error() != ErrorCodes::Success) {
    return;                       // 验证不通过：只回 error，不回用户信息
}
// 通过后：内存里找 UserInfo，找不到就查 MySQL
user_info = MysqlMgr::GetInstance()->GetUser(uid);
_users[uid] = user_info;
rtvalue["uid"]  = uid;
rtvalue["token"] = rsp.token();
rtvalue["name"] = user_info->name;
```

对应的 `StatusServer` 侧（`StatusServiceImpl::Login`）：

```cpp
if (iter == _tokens.end())      -> error = UidInvalid   (1011)  // 没登录过 / token 不认
if (iter->second != token)      -> error = TokenInvalid (1010)  // token 对不上
否则                             -> error = Success (0)
```

错误码对照（`ChatServer/const.h`）：`1001` JSON 解析失败、`1002` RPC 失败、`1010` Token 失效、`1011` uid 无效。

---

## 2. 客户端：登录后的基本界面

### 新增的 18 个文件

| 文件 | 作用 |
|---|---|
| `chatdialog.h/.cpp/.ui` | **聊天主界面**：左侧会话/联系人列表，右侧聊天区 |
| `chatuserlist.h/.cpp` | 左侧列表控件（`QListWidget` 派生），负责批量装载条目 |
| `chatuserwid.h/.cpp/.ui` | 列表里**单个联系人条目**的外观（头像 / 昵称 / 最后一条消息） |
| `listitembase.h/.cpp` | 自定义列表项基类，处理鼠标悬停/选中 |
| `customizeedit.h/.cpp` | 自定义输入框（带占位提示、清空按钮） |
| `clickedbtn.h/.cpp` | 可点击按钮（按下/悬停状态） |
| **`tcpmgr.h/.cpp`** | **TCP 长连接管理器**（单例），本次的关键 |
| `usermgr.h/.cpp` | 用户信息管理（登录用户、联系人缓存） |

同时修改：`mainwindow.*`（新增 `SlotSwitchChat` 界面切换）、`logindialog.*`（登录成功后接 TCP）、`global.h`（新增 `ID_CHAT_LOGIN` / `ID_CHAT_LOGIN_RSP` / `ServerInfo`）、`llfcchat.pro`、`style/stylesheet.qss`。

### TcpMgr：长连接 + 拆包

- `slot_tcp_connect(ServerInfo)` —— 拿到 `host / port / token` 后发起 `QTcpSocket::connectToHost`
- 连上以后 `emit sig_con_success(true)`
- 收数据时按 `_b_recv_pending → _message_id → _message_len → _buffer` 的顺序**拼包/拆包**（TCP 是字节流，一次 `readyRead` 未必是完整一条消息）
- `_handlers[ReqId] = lambda` 注册回调，`ID_CHAT_LOGIN_RSP` 的回调里 `emit sig_swich_chatdlg()`

---

## 完整链路（登录 → 进入聊天界面）

```
① HTTP 登录（和 day4 一样）

Qt LoginDialog
   │  POST /user_login { email, passwd }
   ▼
CServer1 GateServer ── MysqlMgr::CheckPwd ──▶ MySQL
   │  gRPC GetChatServer(uid)
   ▼
StatusServer ── 挑一台 ChatServer + 生成 token ──▶ 回 host / port / token
   │
   ▼
Qt 拿到 error=0, uid, token, host, port

② TCP 长连接 + token 验证（day5 新增）

Qt LoginDialog
   │  emit sig_connect_tcp(ServerInfo)
   ▼
TcpMgr::slot_tcp_connect ── connectToHost(host, port) ──▶ ChatServer :8090
   │  连上：emit sig_con_success(true)
   │
   │  emit sig_send_data(ID_CHAT_LOGIN, {uid, token})     ← 4 字节头 + JSON
   ▼
ChatServer :: CSession 收包 ──▶ LogicSystem::LoginHandler
   │
   │  gRPC Login(uid, token)
   ▼
StatusServer ── 查 _tokens[uid] ──┬─ 不匹配 → error 1010 / 1011
                                  └─ 匹配   → error 0
   │
   ▼
ChatServer ── 通过后查/缓存 UserInfo（MySQL）──▶ 回 {uid, token, name}
   │  msg_id = MSG_CHAT_LOGIN_RSP (1006)
   ▼
TcpMgr::handleMsg ──▶ _handlers[ID_CHAT_LOGIN_RSP] ──▶ emit sig_swich_chatdlg()
   │
   ▼
MainWindow::SlotSwitchChat ──▶ ChatDialog（聊天主界面）
```

---

## 与 day4 的差别（`StatusServer` 的三处调整）

| 项 | day4 | day5 |
|---|---|---|
| token 存放 | 写进 Redis（`user_token_<uid>`） | **进程内 `unordered_map<int,string> _tokens` + 互斥锁**（`RedisMgr` / `DistLock` 不再参与，`DistLock.h/.cpp` 已删除） |
| 挑服务器 | `[chatservers]` 列表 + 按连接数（注释状态），实际返回第一台 | 直接读 `[ChatServer1]` / `[ChatServer2]` 两段，`getChatServer()` 按 `con_count` 取最小 |
| 端口 | `8990` / `8991` | **`8090` / `8091`**（`ChatServer` 自己监听 8090） |

另外：`StatusServiceImpl::Login` 里的分支判定本次已经理顺（`_tokens` 里查不到 → `UidInvalid`，对不上 → `TokenInvalid`）。

`CServer1` 只改了 `LogicSystem.cpp`；`VarifyServer` 一个字节都没动。

---

## 环境要求

| 组件 | 版本 / 说明 |
|---|---|
| Visual Studio | 2022（v143 工具集），用于编译 ChatServer / CServer1 / StatusServer |
| vcpkg | 依赖：boost、protobuf、grpc、jsoncpp、hiredis |
| MySQL | Connector/C++（`mysqlcppconn`），库名 `llfc`，本机实测端口 3308 |
| Qt | 6.8.x MinGW 64-bit（llfcchat） |
| Node.js | 18+，用于 VarifyServer |
| Redis | 验证码缓存（本机实测端口 6380） |
| 163 邮箱 | 需开启 SMTP 并获取授权码 |

> `mysqlcppconn-9-vs14.dll` / `mysqlcppconn8-2-vs14.dll` 是 C 端服务运行期依赖（合计约 25 MB），
> 属于二进制产物，**未提交**。自己跑的话需要自备 MySQL Connector/C++ 并放到可执行文件旁边。

---

## 编译 / 运行

### 四个服务端的启动顺序

```bash
# 1) 状态服务器（先起，ChatServer 和 CServer1 都要连它）
#    VS2022 打开 StatusServer.sln 生成，运行 StatusServer.exe（:50052）

# 2) 验证码服务
cd VarifyServer
npm install
node server.js                       # :50051

# 3) 聊天服务器
#    VS2022 打开 ChatServer.sln 生成，运行 ChatServer.exe（:8090）

# 4) 网关服务器
#    VS2022 打开 CServer1.sln 生成，运行 CServer1.exe（:8080）
```

运行前保证 `config.ini` 与各自的 exe 在同一目录。

### 端口一览

| 服务 | 端口 |
|---|---|
| CServer1（HTTP 网关） | 8080 |
| VarifyServer（gRPC） | 50051 |
| StatusServer（gRPC） | 50052 |
| ChatServer（TCP 长连接） | 8090（配置里还预留了 8091） |
| MySQL | 3308 |
| Redis | 6380 |

### llfcchat（Qt 客户端）

用 Qt Creator 打开 `llfcchat.pro`，选择 Desktop Qt 6.8.x MinGW 64-bit 套件构建运行。
`config.ini` 会被 `.pro` 的 `QMAKE_POST_LINK` 自动拷到 exe 同目录。

---

## ⚠️ 关于配置文件（重要）

### VarifyServer/config.json —— 未提交

`VarifyServer/config.json` **没有提交**，因为它包含真实凭据（163 邮箱账号 + SMTP 授权码、Redis 密码）。
仓库里提供的是模板 [`VarifyServer/config.example.json`](VarifyServer/config.example.json)：

```bash
cd VarifyServer
cp config.example.json config.json
# 然后填入自己的邮箱、授权码、Redis 密码
```

### 各服务端的 config.ini —— 已提交，请按需修改

`CServer1/config.ini`、`ChatServer/config.ini`、`StatusServer/config.ini`、`llfcchat/config.ini`
随源码一起提交，里面是**本地学习环境**的端口与占位密码（`127.0.0.1` / `123456`）。
若你往里填了自己的服务器地址或真实密码，注意本仓库是**公开仓库**，提交前务必先脱敏。

---

## 说明与约定

1. **不提交构建产物**：`.vs/`、`x64/`、`build/`、`node_modules/`、`moc_*`、`ui_*.h`、`*.pdb`、`*.ilk`、
   `*.dll` 等全部忽略。原始目录合计 10 GB 以上，过滤后本分支只有 4 MB 出头。
2. **protobuf 生成文件是提交的**：`message.pb.h/.cc`、`message.grpc.pb.h/.cc` 由 `protoc` 从 `message.proto` 生成，
   保留是为了克隆后可直接编译（Windows 上配 protoc + grpc_cpp_plugin 较繁琐）。
3. **四份 `message.proto` 必须同步**：`ChatServer/`、`CServer1/`、`StatusServer/`、`VarifyServer/` 各有一份独立拷贝，
   改接口时每一份都要改，否则 gRPC 会出现字段不匹配。
4. **多份 `const.h` / `global.h` 的错误码与消息 id 要对齐**：服务端 `const.h` 与客户端 `global.h` 里
   `MSG_CHAT_LOGIN = 1005` / `MSG_CHAT_LOGIN_RSP = 1006` 必须一致，否则消息会分发不到处理函数。
5. **二进制依赖不提交**：`mysqlcppconn*.dll` 需要自行准备。
6. `llfcchat/timberbtn.cpp/.h` 是历史遗留的空壳文件（未被 `.pro` 收录），保留但**不参与编译**。
