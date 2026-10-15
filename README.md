# code — day8 分支

> **关于本仓库的组织方式**：按「一天一个分支」归档。
> 本 `day8` 分支里放的是**第 8 天**的代码 —— 本次是**客户端 + 服务端一起动**，
> 6 个工程（1 个 Qt 客户端 + 5 个 C++/Node 服务端）平铺在根目录，外加一份数据库脚本 `xulei.sql`。
> 第 1 ~ 4 天的代码在 **`master` 分支**（按 `day1/` ~ `day4/` 文件夹归档）；
> 第 5 天（4 个服务端 + Qt 客户端）在 **`day5` 分支**；第 6 天（只动了 Qt 客户端）在 **`day6` 分支**；
> 第 7 天（只动服务端，拆双聊天节点）在 **`day7` 分支**。
> 在 GitHub 页面左上角的**分支下拉框**里切换即可对比查看。

本仓库**只提交源码、工程文件与资源**，编译产物与缓存已在 `.gitignore` 中全部排除。

> ⚠️ **本次备注（来自提交时）**：**数据库转储还没有完成**。
> 根目录的 `xulei.sql` 是**当前能导出的部分**（3 张表 + 1 个存储过程），
> 不保证与代码里用到的表结构完全一致，见下文 [「关于 xulei.sql」](#关于-xuleisql)。

---

## 本次完成：用户搜索 → 加好友，客户端到服务端打通

一句话：**把「搜索框搜人 → 找到对方 → 发好友申请」这条链路从界面一直铺到了数据库**，同时把各服务的 Redis 连接池重写成了带 AUTH 认证与保活线程的版本。

| # | 内容 | 说明 |
|---|---|---|
| 1 | **客户端补齐「搜索用户」链路** | 新增 `FindFailDlg`（搜索失败弹窗）；`searchlist.cpp` 从占位实现改成真正发 `ID_SEARCH_USER_REQ` 并等待 `ID_SEARCH_USER_RSP`；`tcpmgr.cpp` 新增该回包的处理函数 |
| 2 | **服务端实现搜索** | `ChatServer` / `ChatServer2` 的 `LogicSystem` 新增 `SearchInfo`，按「纯数字走 uid / 否则走名字」分流到 `GetUserByUid` / `GetUserByName`，都带 Redis 优先 + MySQL 兜底 |
| 3 | **加好友申请落库** | `MysqlDao` 新增 `AddFriendApply(from,to)`，写 `friend_apply` 表；`LogicSystem::AddFriendApply` 接客户端 `ID_ADD_FRIEND_REQ` |
| 4 | **Redis 连接池重写** | `CServer1` / `StatusServer` / `ChatServer` / `ChatServer2` 的 `RedisMgr` 全部改成 `RedisConPool`：连接时 `AUTH`、带 60s 一次的 `PING` 保活线程、`Defer` 自动归还 |
| 5 | **用户资料字段扩展** | `message.proto` 的 `UserInfo` 增加 `icon / nick / sex`，`touid` 后移；端到端（proto → 服务端 → 客户端 `SearchInfo`）同步补齐 |

---

## 工程（本次 6 个：1 客户端 + 5 服务端）

| 工程 | 技术栈 | 职责 | 本次状态 |
|---|---|---|---|
| [`llfcchat`](llfcchat) | Qt 6 / C++ / QWidget | **桌面客户端**。登录、搜索好友、加好友、聊天界面 | 🆕 +3 文件（`findfaildlg.*`），改 16 个 |
| [`CServer1`](CServer1) | C++ / cpp-httplib / gRPC / Redis / MySQL | **HTTP 网关**。注册、登录、找回密码，转发验证码请求，把用户分派到聊天节点 | 🔧 改 4 个（`RedisMgr.*` 重写） |
| [`StatusServer`](StatusServer) | C++ / gRPC / Redis / MySQL | **状态服务**。发 token、校验登录、按连接数挑聊天节点 | 🔧 改 4 个（`RedisMgr.*` 重写） |
| [`ChatServer`](ChatServer) | C++ / Boost.Asio / gRPC / Redis / MySQL | **聊天节点 1**（chatserver1）。客户端 TCP 长连接 + 对端 gRPC | 🔧 改 15 个（搜索 / 加好友 / Redis 池） |
| [`ChatServer2`](ChatServer2) | 同上 | **聊天节点 2**（chatserver2），与节点 1 同构 | 🔧 改 15 个（与节点 1 同步） |
| [`VarifyServer`](VarifyServer) | Node.js / gRPC / nodemailer / ioredis | **验证码服务**。发邮件验证码、校验验证码 | ➕ 新增 `redis123.js`（本地测试脚本） |

---

## 1. 系统拓扑

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
│  · 客户端 TCP  :8090  │◀──gRPC 互调───────▶│  · 客户端 TCP  :8091  │
│  · 对端 gRPC :50055   │   50055 ⇄ 50056   │  · 对端 gRPC :50056   │
└───────┬───────────────┘                   └───────────┬───────────┘
        │                                               │
        └───────────────┬───────────────────────────────┘
                        │
              ┌─────────▼─────────┐   ┌──────────────┐
              │ Redis     :6380   │   │ MySQL  :3308 │
              │ · logincount 计数 │   │ 库名 xulei   │
              │ · ubaseinfo_ 缓存 │   └──────────────┘
              └───────────────────┘
```

> 客户端的一条「搜索」请求，实际路径是：
> **Qt 客户端 → ChatServer（TCP 长连接，`ID_SEARCH_USER_REQ`）→ Redis / MySQL → 回包 `ID_SEARCH_USER_RSP` → 客户端弹窗**。

---

## 2. 本次的核心改动

### 2.1 客户端：搜索链路（从占位到真发请求）

`searchlist.cpp` 在 `day6` 里还是占位实现（写死一个「人机 / llfc」的假 `SearchInfo`、弹成功窗）。
本次改为：**从搜索框取文本 → 发 TCP 请求 → 收到回包再决定弹「成功」还是「失败」。**

```cpp
// 点搜索：先锁住「等待中」，再发请求
if(_send_pending) {
    return;                    // 上一次搜索还没回来，忽略
}
if(!_search_edit) {            // ← 注意这里是否定判断（day6 快照里曾写成 if(_search_edit)，是反的）
    qDebug() << "_search_edit为空";
    return;
}
waitPending(true);             // 弹 LoadingDlg、置 pending
auto uid_str = dynamic_cast<CustomizeEdit*>(_search_edit)->text();
QJsonObject jsonObj;
jsonObj["uid"] = uid_str;      // 字段名仍叫 uid，但内容可以是「数字」或「名字」
emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_SEARCH_USER_REQ, jsonData);
```

回包处理新增在 `tcpmgr.cpp`：

```cpp
_handlers.insert(ID_SEARCH_USER_RSP, [this](ReqId id, int len, QByteArray data) {
    // 解析 JSON；error != SUCCESS → emit sig_user_search(nullptr)
    // 成功 → 组装 SearchInfo(uid, name, nick, desc, sex, icon) → emit sig_user_search(info)
});
```

`searchlist` 侧收到结果后：

| 情况 | 表现 |
|---|---|
| `nullptr` | 弹 `FindFailDlg`（新增的「查无此人」弹窗） |
| 搜到自己 | 直接 `return`（暂不处理，留给后续逻辑扩充） |
| 搜到别人 | 弹 `FindSuccessDlg` 并 `SetSearchInfo(si)` |

新增文件：

| 文件 | 作用 |
|---|---|
| `findfaildlg.h/.cpp/.ui` | 搜索失败弹窗（图标 + 提示 + 确定按钮），已在 `llfcchat.pro` 注册 |

### 2.2 服务端：搜索的用户名/uid 分流

`ChatServer`（和 `ChatServer2`）的 `LogicSystem::SearchInfo` 是这条链路的第一站：

```cpp
bool b_digit = isPureDigit(uid_str);   // 判断搜索框里是不是「纯数字」
if (b_digit) {
    GetUserByUid(uid_str, rtvalue);    // 数字 → 按 uid 查
} else {
    GetUserByName(uid_str, rtvalue);   // 否则 → 按 name 查
}
```

两个查询都是**先查 Redis 缓存，未命中再落 MySQL**，命中或查到后把用户信息写回 Redis：

| 查询 | Redis key | MySQL 兜底 |
|---|---|---|
| `GetUserByUid` | `ubaseinfo_<uid>` | `MysqlDao::GetUser(int uid)` |
| `GetUserByName` | `nameinfo_<name>` | `MysqlDao::GetUser(std::string name)` |

> ⚠️ 这里的 **Redis 命中即直接返回**，不再回数据库核对 —— 是本次一个已知隐患，见 [「已知问题」](#5-已知问题)。

### 2.3 加好友申请落库

客户端 `applyfriend.cpp` 由「注释掉的伪代码」改成真正发请求：

```cpp
jsonObj["uid"]     = UserMgr::GetInstance()->GetUid();  // 申请人
jsonObj["applyname"] = ...;   // 对方备注名
jsonObj["bakname"]   = ...;   // 我方给对方的备注
jsonObj["touid"]     = _si->_uid;                        // 被申请人
emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_ADD_FRIEND_REQ, jsonData);
```

服务端 `LogicSystem::AddFriendApply` → `MysqlMgr::AddFriendApply(uid, touid)` → `MysqlDao::AddFriendApply`：

```sql
INSERT INTO friend_apply (from_uid, to_uid) VALUES (?, ?)
ON DUPLICATE KEY UPDATE from_uid = from_uid, to_uid = to_uid
```

### 2.4 Redis 连接池重写

`CServer1` / `StatusServer` / `ChatServer` / `ChatServer2` 的 `RedisMgr` 本次统一换成 **`RedisConPool`**，比 day5/day7 的版本多了三件事：

1. **建连即 `AUTH`**：`redisConnect` 后立刻 `redisCommand(ctx, "AUTH %s", pwd)`，认证失败直接丢弃该连接；
2. **保活线程**：后台线程每 60 秒调一次 `checkThread()` 发 `PING`，把被 Redis 服务端掐掉的空闲连接救回来；
3. **`std::atomic` 计数 + `b_stop_` 停止位**：析构时能安全收尾。

改动量最大的两个文件：

| 工程 | 文件 | 行数变化 |
|---|---|---|
| `CServer1` | `RedisMgr.cpp` | **+287 / −526**（整体重写） |
| `CServer1` | `RedisMgr.h` | **+158 / −85** |
| `StatusServer` | `RedisMgr.cpp` | **+113 / −44** |
| `ChatServer` / `ChatServer2` | `RedisMgr.cpp` | **+89 / −44** |

### 2.5 用户资料字段扩展（proto + 端到端）

`message.proto` 里 `UserInfo` 新增三个字段，`touid` 号位后移：

```proto
message UserInfo {
    int32  uid    = 1;
    string name   = 2;
    string email  = 3;
    string icon   = 4;   // 新增：头像
    string nick   = 5;   // 新增：昵称
    int32  sex    = 6;   // 新增：性别
    int32  touid  = 7;   // 原为 4
}
```

生成物 `message.pb.h/.cc` 随之重新生成（`ChatServer` / `ChatServer2` 各 +194/−3、+153/−50）。
客户端 `userdata.h` 的 `SearchInfo` / `AddFriendApply` 构造函数、`UserInfo` 也补上了 `nick / sex / icon`。

---

## 3. 相对 day7 / day6 的差异（逐文件 MD5 比对）

> 用 `Get-FileHash -Algorithm MD5` 把每个文件和上一批的对应文件比了一遍。

### 服务端（vs `day7` 分支）

| 工程 | 新增 | 修改 | 说明 |
|---|---|---|---|
| `ChatServer` | 0 | **15** | `LogicSystem.cpp` +243（搜索/加好友）、`MysqlDao.cpp` +72（`AddFriendApply` + `GetUser(name)`）、`RedisMgr.*` 重写、`message.proto`/`message.pb.*` 加字段、`const.h` +11（消息号）、`ChatGrpcClient.cpp` +24、`ChatServiceImpl.cpp` +28、`config.ini`（Schema → `xulei`） |
| `ChatServer2` | 0 | **15** | 与 `ChatServer` 同步；另修 `config.ini`：`Port` 8090 → **8091**、`Schema` → `xulei` |
| `CServer1` | 0 | **4** | `RedisMgr.cpp/.h` 整体重写；`LogicSystem.cpp` +25（测试路由 `/hello`、`/ch2_3_war_exploded`）；`config.ini` Schema → `xulei` |
| `StatusServer` | 0 | **4** | `RedisMgr.cpp/.h` 重写；`StatusServiceImpl.cpp` +6（把原先「启动时清 logincount」的两段 `HSet` **注释掉**了）；`config.ini` Schema → `xulei` |
| `VarifyServer` | **1** | 0 | 新增 `redis123.js`（与 `redis.js` 内容一致的本地测试脚本） |

### 客户端（vs `day6` 分支）

| 类别 | 文件 |
|---|---|
| **新增（3）** | `findfaildlg.cpp`、`findfaildlg.h`、`findfaildlg.ui` |
| **修改（16）** | `searchlist.cpp`（+55/−5）、`tcpmgr.cpp`（+114/−4，新增 `ID_SEARCH_USER_RSP` 处理）、`tcpmgr.h`（+7/−2）、`applyfriend.cpp`（+18/−18）、`userdata.cpp/.h`（+7/−3，加 nick/sex/icon）、`applyfriendpage.*`、`applyfrienditem.h`、`applyfriendlist.h`、`contactuserlist.h`、`chatdialog.cpp`（补 `SetSearchEdit`）、`global.h`（新增 `ID_TEXT_CHAT_MSG_*` 消息号）、`logindialog.cpp`、`mainwindow.cpp`、`llfcchat.pro` |

---

## 4. 配置说明

### 端口总表

| 服务 / 端口 | 值 | 配置位置 |
|---|---|---|
| HTTP 网关 | 8080 | `CServer1/config.ini` `[GateServer]` |
| VarifyServer gRPC | 50051 | `[VarifyServer]` |
| StatusServer gRPC | 50052 | `StatusServer/config.ini` `[StatusServer]` |
| ChatServer 客户端 TCP | 8090 | `ChatServer/config.ini` `[SelfServer] Port` |
| ChatServer 对端 gRPC | 50055 | `[SelfServer] RPCPort` |
| ChatServer2 客户端 TCP | **8091** | `ChatServer2/config.ini` `[SelfServer] Port` |
| ChatServer2 对端 gRPC | 50056 | `[SelfServer] RPCPort` |
| MySQL | 3308 | `[Mysql]`，库名 **`xulei`** |
| Redis | 6380 | `[Redis]`，密码 `123456` |

> **本次一个关键变化**：4 个 C++ 工程的 `config.ini` 里 `[Mysql] Schema` 从 `llfc` 改成了 **`xulei`**，
> 也就是实际连的库换成了 `xulei` 库（这正是下文「已知问题」里那个表结构不匹配的来源）。

### 节点注册表（StatusServer 视角）

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

`StatusServer` 登录时对每个节点查 `HGET logincount <name>`，**取连接数最小的那个**返回给网关。

---

## 5. 已知问题

> 这些是**归档时刻的代码原貌**，本分支只做归档、未作任何修改，留给后续进度补齐。

| # | 问题 | 影响 |
|---|---|---|
| 1 | **`xulei` 库的 `user` 表只有 5 列**（`id/uid/name/email/pwd`），而 `MysqlDao::GetUser(std::string name)` 读的是 `nick/desc/sex` | 按**名字**搜索会因 SQL 异常走到失败分支 → 客户端弹「查无此人」；**只有该名字在 Redis `nameinfo_` 里有缓存时才能搜到** |
| 2 | `GetUserByUid` / `GetUserByName` **Redis 命中即返回**，不回 MySQL 核对 | 若 Redis 里残留了旧数据（换库 / 改过 uid），会搜出**与当前库里不对应的人** |
| 3 | `ChatGrpcClient` 构造函数读的段名是 **`[ProtoServer]`**，而两个 `config.ini` 里写的是 `[PeerServer]`；取端口用的键是 **`"Post"`** 而不是 `"Port"` | 节点间的 gRPC 连接池目前**建不出来**（`_pools` 为空） |
| 4 | `ChatServiceImpl` 的三个 `Notify*` RPC、`ChatGrpcClient` 的三个同名方法仍是**空实现** | 跨节点投递消息还没落地 |
| 5 | `ChatGrpcClient::GetBaseInfo` 与 `ChatServiceImpl::GetBaseInfo` 在 Redis 命中分支**没有 `return`** | 函数走到末尾无返回值（未定义行为） |
| 6 | `isPureDigit("")` 返回 **true** | 空搜索串会落到 `std::stoi("")` 抛异常 |
| 7 | 两个 `config.ini` 里对端 `[chatserver2]` 段：`ChatServer2` 那份的段名/`Name` 与内容对不上（段名仍是 `chatserver2` 却写 `Name=chatserver1`），且 `Host` 都写成 `127.0.0.127` | 配置语义混乱（因问题 3 目前还没实际生效） |
| 8 | `StatusServer/StatusServiceImpl.cpp` 把「启动时把 logincount 清零」的两段 `HSet` 注释掉了 | 节点重启后计数不会归零，负载分派会偏 |

---

## 关于 `xulei.sql`

> 提交备注：**数据库转储还没有完成**。这份脚本是**当前能导出的部分**，不代表完整 schema。

文件在仓库根目录，内容为：

| 对象 | 说明 |
|---|---|
| 表 `user` | **只有 5 列**：`id / uid / name / email / pwd`。含 2 行测试用户：`xl`（`uid=2`）与 `ly`（`uid=3`），密码均为明文（学习环境占位） |
| 表 `friend_apply` | 好友申请表（`from_uid` / `to_uid` 等） |
| 表 `user_id` | uid 自增计数表（当前 `id=3`） |
| 存储过程 `reg_user` | 注册时分配 uid |

**这不是一份可以直接把整套服务跑起来的完整 schema** —— 代码里还用到了 `nick / desc / sex / icon` 等列、
以及 `chat_thread` / `private_chat` / `group_chat` 等表，这些**本次都还没导出**。
等正式转储完成后会补上。

> ⚠️ 这份 `xulei.sql` 来自**本地学习环境**，含真实测试邮箱与明文密码（`xl` = `745899278@qq.com`、`ly` = `15869988660@163.com`）。
> 公开仓库仅作学习归档；**请勿在正式环境复用**，也不要往里面填你自己的真实密码。

---

## 环境要求

| 组件 | 版本 / 说明 |
|---|---|
| Qt | 6.8.3 MinGW（客户端 `llfcchat`） |
| Visual Studio | 2022（MSVC v143），x64 + **Debug** 配置（4 个 C++ 服务端） |
| gRPC / protobuf | 1.34.0 / 3.13.0（各工程用 `PropertySheet1.props` 统一引依赖） |
| MySQL | 端口 3308，库名 `xulei` |
| Redis | 端口 6380，密码 `123456` |
| Node.js | 跑 `VarifyServer` 用；`nodemailer` 发邮件 |

> `mysqlcppconn-9-vs14.dll` / `mysqlcppconn8-2-vs14.dll`（各约 15 MB / 9 MB）属于**运行时依赖**，
> 为了不把仓库撑大，这里**没有提交**，请从本机 MySQL Connector/C++ 安装目录自行拷贝到 exe 同级目录。

---

## 编译 / 运行

```bash
# 0) 起 Redis（6380，密码 123456）与 MySQL（3308），导入库
# 1) 按顺序启动：VarifyServer → StatusServer → ChatServer → ChatServer2 → CServer1
#    4 个 C++ 工程用 VS2022 打开对应的 .sln，选 x64 / Debug 生成
#    客户端 llfcchat 用 Qt Creator 打开 llfcchat.pro 生成

# VarifyServer（Node.js）
cd VarifyServer
npm install
cp config.example.json config.json   # 然后填入真实的邮箱授权码与 Redis 密码
node server.js
```

`config.ini` 会被各工程的属性表后置事件拷到 `x64\Debug\`，
**改完 `config.ini` 要重新生成一次才生效**。

---

## ⚠️ 关于配置文件

- `VarifyServer/config.json` **含真实的 163 邮箱 SMTP 授权码与 Redis 密码，已在 `.gitignore` 中排除，不在本仓库里**。
  仓库只提供模板 `VarifyServer/config.example.json`，使用时复制成 `config.json` 再填自己的值。
  如果你的仓库历史里曾出现过 `config.json`，请到邮箱服务商后台**重新生成授权码**。
- 其余 `config.ini` 里的 `User=llfc` / `Passwd=123456` 是**本地学习环境的占位密码**，随源码提交。
- `xulei.sql` 含真实测试邮箱与明文密码（见上文说明）。
- 本仓库是**公开仓库**，往配置里填真实地址或密码前请先确认已脱敏。

---

## 说明与约定

1. **不提交构建产物**：`.vs/`、`x64/`、`项目名/项目名/`（VS 嵌套中间目录）、`*.vcxproj.user`、
   `*.dll`、`*.exe`、`*.pdb`、`*.bak` 等全部忽略。
2. **protobuf 生成物保留**：`message.pb.*` / `message.grpc.pb.*` 已提交，
   这样克隆下来不必自备 protoc 与 grpc_cpp_plugin 就能编译。
3. **本分支同时含客户端与 5 个服务端**（与 `day7` 只含服务端不同）。
4. 两个节点工程的源码内容基本一致，只靠 `config.ini` 区分身份 —— 这是刻意的：
   同一份代码换个配置就是另一个节点。
