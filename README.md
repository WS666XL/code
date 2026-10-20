# code — day9 分支（**第一季完成**）

> **关于本仓库的组织方式**：按「一天一个分支」归档。本 `day9` 分支里放的是**第 9 天（第一季收尾）**的代码 ——
> 6 个工程（1 个 Qt 客户端 + 5 个 C++/Node 服务端）平铺在根目录，数据库脚本 `xl.sql` 放在 `CServer1/` 里。
>
> | 分支 | 内容 |
> |---|---|
> | `master` | 第 1 ~ 4 天的早期归档（`day1/` ~ `day4/` 文件夹 + 早期单工程目录） |
> | `day5` | 4 个服务端 + Qt 客户端 |
> | `day6` | **只有** Qt 客户端 |
> | `day7` | **只有**服务端（首次拆出双聊天节点） |
> | `day8` | 客户端 + 服务端：搜索用户 → 加好友 |
> | **`day9`（本分支）** | **客户端 + 服务端：好友认证 + 双节点文本聊天 = 第一季完结** |
>
> 在 GitHub 页面左上角的**分支下拉框**里切换即可对比查看。

本仓库**只提交源码、工程文件与资源**，编译产物与缓存已在 `.gitignore` 中全部排除。

> ⚠️ **本次备注（原话）**：**第一季完成**。
> 至此「注册 → 登录 → 加好友 → 通过申请 → 双节点互发消息」这条主线全链路跑通，
> 真正的 IM 骨架已经立起来了（还差消息落库与离线消息，见 [「已知问题」](#7-已知问题)）。

---

## 本次完成：好友认证闭环 + 双聊天节点文本聊天

一句话：**把 day8 停在「发出好友申请」的地方，补齐成「收到申请 → 同意 → 进好友列表 → 打开聊天窗口 → 跨节点把消息投到对方屏幕上」。**

| # | 内容 | 落点 |
|---|---|---|
| 1 | **好友申请列表下发** | `ChatServer` 登录回包里新增 `apply_list`（好友申请）与 `friend_list`（好友列表）两个数组，客户端 `UserMgr` 收下后按页切片 |
| 2 | **同意/拒绝好友申请** | 客户端新增 `AuthenFriend` 认证对话框（备注标签可增删）；服务端 `AuthFriendApply` 把 `friend_apply.status` 置 1，`AddFriend` 双向写 `friend` 表 |
| 3 | **跨节点好友认证通知** | `ChatGrpcClient` 读 `[PeerServer]` 段取对端地址，`NotifyAuthFriend` 真正发起 RPC；`ChatServiceImpl::NotifyAuthFriend` 在对端进程里找到目标 session 并下发 `ID_NOTIFY_AUTH_FRIEND_REQ` |
| 4 | **文本聊天端到端** | 客户端 `ChatPage` 发 `ID_TEXT_CHAT_MSG_REQ`；服务端 `DealChatTextMsg` 判断对方是否在本节点 —— **在本节点直投**，**在另一节点走 gRPC `NotifyTextChatMsg`** |
| 5 | **客户端聊天界面成型** | `ChatDialog::slot_append_send_chat_msg` / `slot_text_chat_msg` 把左右气泡塞进 `ChatPage`；新增好友资料页 `FriendInfoPage`；样式表补 98 行 |
| 6 | **网关注册改造 + 数据库转储** | `CServer1` 注册改为**显式事务** `RegUserTransaction`（不再依赖存储过程），并把头像 `icon`、昵称 `nick` 一并入库；库名换成 **`xl`**，配套导出 `CServer1/xl.sql` |

---

## 工程（本次 6 个：1 客户端 + 5 服务端）

| 工程 | 技术栈 | 职责 | 本次状态 |
|---|---|---|---|
| [`llfcchat`](llfcchat) | Qt 6.8.3 / C++ / QWidget | **桌面客户端**。登录、搜索、加好友、认证、聊天窗口 | 🆕 +6 源文件（`authenfriend.*`、`friendinfopage.*`），改 27 个 |
| [`CServer1`](CServer1) | C++ / cpp-httplib / gRPC / Redis / MySQL | **HTTP 网关**（:8080）。注册、登录、找回密码、分派聊天节点 | 🔧 改 6 个（注册改事务 + 传 icon），🆕 `data.h` / `xl.sql` |
| [`StatusServer`](StatusServer) | C++ / gRPC / Redis / MySQL | **状态服务**（:50052）。发 token、校验登录、按连接数挑节点 | 🔧 改 6 个（`UserInfo` 抽到 `data.h`、`unique_ptr` 规范化） |
| [`ChatServer`](ChatServer) | C++ / Boost.Asio / gRPC / Redis / MySQL | **聊天节点 1**（:8090）。客户端 TCP 长连接 + 对端 gRPC（:50055） | 🔧 改 10 个（好友列表 / 认证 / 文本消息 / 跨节点通知） |
| [`ChatServer2`](ChatServer2) | 同上 | **聊天节点 2**（:8091 / :50056），与节点 1 同构 | 🔧 改 10 个（与节点 1 同步） |
| [`VarifyServer`](VarifyServer) | Node.js / gRPC / nodemailer / ioredis | **验证码服务**（:50051） | ✅ 代码零改动（仅补回 `config.example.json` 模板） |

> 文件量：6 个工程共 **327 个文件 / 约 6.9 MB**（`llfcchat` 173 / `ChatServer` 40 / `ChatServer2` 40 / `CServer1` 36 / `StatusServer` 28 / `VarifyServer` 10），加上 `.gitignore` 与 `README.md` 一起是 329 个。

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
┌───────▼───────────────┐   gRPC (本次真正打通)  ┌───────▼───────────────┐
│ ChatServer  (node1)   │◀───── 50055 ⇄ 50056 ────▶│ ChatServer2 (node2)  │
│  · 客户端 TCP  :8090  │                        │  · 客户端 TCP  :8091  │
└───────┬───────────────┘                        └───────┬───────────────┘
        │                                               │
        └───────────────┬───────────────────────────────┘
                        │
              ┌─────────▼─────────┐   ┌──────────────┐
              │ Redis     :6380   │   │ MySQL  :3308 │
              │ · logincount 计数 │   │ 库名 xl      │
              │ · ubaseinfo_ 缓存 │   │ 4 表 + 1 过程│
              └───────────────────┘   └──────────────┘
```

两条主链路的实际走向：

```
① 加好友认证
   客户端(同意) ──ID_AUTH_FRIEND_REQ──▶ ChatServer(nodeA)
        └─▶ friend_apply.status=1 + friend 双向插入
        └─▶ 查 Redis "uip_<touid>" 得对方所在节点名
              ├─ 同节点：本进程 UserMgr 找到 session → 直接下发 ID_NOTIFY_AUTH_FRIEND_REQ
              └─ 异节点：gRPC NotifyAuthFriend(nodeB) → nodeB 找到 session → 下发

② 文本聊天
   客户端(发送) ──ID_TEXT_CHAT_MSG_REQ──▶ ChatServer(nodeA)
        └─▶ 立刻回 ID_TEXT_CHAT_MSG_RSP（本条消息的回执）
        └─▶ 查 Redis "uip_<touid>"
              ├─ 同节点：直接 session->Send(ID_NOTIFY_TEXT_CHAT_MSG_REQ)
              └─ 异节点：gRPC NotifyTextChatMsg(nodeB) → nodeB 下发通知
```

> 关键点：**Redis 里存的是「用户 → 所在节点名」，不是 IP**（键 `uip_<uid>`，值形如 `chatserver1`），
> 拿它和 `[SelfServer] Name` 一比就知道该不该走 gRPC。

---

## 2. 服务端改动

### 2.1 登录回包多带两张表（`ChatServer` / `ChatServer2`）

`LogicSystem` 收到 `ID_CHAT_LOGIN` 后，除了返回自己的资料，还会把**好友申请**与**好友列表**塞进回包：

```json
{
  "error": 0,
  "uid": 1064, "name": "xl", "nick": "xl", "icon": ":/res/head_4.jpg", "sex": 0,
  "apply_list":  [ { "uid":1055, "name":"ocz", "nick":"ocz", "desc":"", "icon":"...", "sex":0, "status":1 } ],
  "friend_list": [ { "uid":1065, "name":"ly",  "nick":"ly",  "desc":"", "icon":"...", "sex":0, "back":"ly" } ]
}
```

对应两个新查询（都在 `MysqlDao`）：

| 方法 | SQL 要点 |
|---|---|
| `GetApplyList(touid, list, offset, limit)` | `friend_apply JOIN user`，按 `to_uid = ?` 取申请，带上申请人的 `name/nick/icon/sex/desc` 与 `status` |
| `GetFriendList(self_id, list)` | `friend JOIN user`，`self_id = ?`，取回 `back`（我方给对方的备注） |

### 2.2 加好友认证（`LogicSystem::AuthFriendApply`）

```cpp
// 先落库：把申请标记为已通过，再双向插 friend 表（INSERT IGNORE，天然幂等）
MysqlMgr::GetInstance()->AuthFriendApply(uid, touid);          // UPDATE friend_apply SET status=1
MysqlMgr::GetInstance()->AddFriend(uid, touid, back_name);     // 双向两条记录

// 再按 Redis 里的节点名决定「本机直投」还是「gRPC 转发」
if (to_ip_value == self_name) {   // 对方就在我这个节点
    auto session = UserMgr::GetInstance()->GetSession(touid);
    session->Send(notify.toStyledString(), ID_NOTIFY_AUTH_FRIEND_REQ);
} else {                          // 对方在另一个节点
    ChatGrpcClient::GetInstance()->NotifyAuthFriend(to_ip_value, auth_req);
}
```

> 这里 `[SelfServer] Name` 与 `uip_<uid>` 的值是**同一套命名**（`chatserver1` / `chatserver2`），
> 所以比较的是**字符串**而不是 IP —— 这也是天 7 遗留的配置问题（`[ProtoServer]` 段名写错、端口键写成 `"Post"`）本次一并修好了。

### 2.3 文本聊天（`LogicSystem::DealChatTextMsg`）

```cpp
auto arrays = root["text_array"];                  // 客户端一次可带多条（合并发送）
Json::Value rtvalue;                               // 回执：原样带回 error/text_array/fromuid/touid
Defer defer([&] { session->Send(rtvalue.toStyledString(), ID_TEXT_CHAT_MSG_RSP); });

// 找对方所在节点，不在本节点就走 gRPC
if (to_ip_value == self_name) { ... session->Send(..., ID_NOTIFY_TEXT_CHAT_MSG_REQ); return; }

TextChatMsgReq text_msg_req;                       // 逐条塞进 protobuf
for (auto& txt_obj : arrays) {
    auto* text_msg = text_msg_req.add_textmsgs();
    text_msg->set_msgid(txt_obj["msgid"].asString());
    text_msg->set_msgcontent(txt_obj["content"].asString());
}
ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, text_msg_req, rtvalue);
```

### 2.4 跨节点 gRPC 三件套（本次从「空壳」变成真跑）

| 位置 | day8 状态 | day9 状态 |
|---|---|---|
| `ChatGrpcClient` 构造 | 读 `[ProtoServer]`、端口键 `"Post"` → **连接池建不出来** | 改读 `[PeerServer]`、键改 `"Port"` → 连上 50055/50056 |
| `ChatGrpcClient::NotifyAuthFriend` | 空实现 | 取连接池 → `stub->NotifyAuthFriend()` → `Defer` 归还 |
| `ChatGrpcClient::NotifyTextChatMsg` | 空实现 | 同上，且把 `textmsgs` 原样带回 `rsp` |
| `ChatServiceImpl::NotifyAuthFriend` | 空实现 | 在对端进程里 `GetSession(touid)`，组装 JSON 后 `Send(ID_NOTIFY_AUTH_FRIEND_REQ)` |
| `ChatServiceImpl::NotifyTextChatMsg` | 空实现 | 把 `textmsgs` 转成 `text_array`，`Send(ID_NOTIFY_TEXT_CHAT_MSG_REQ)` |
| `GetBaseInfo`（两处） | Redis 命中分支**没有 return** | 已补 `return true`（本次顺带修掉的未定义行为） |

### 2.5 网关注册改造（`CServer1`）

day8 之前注册走的是存储过程 `reg_user`；本次改成**显式事务**，并且把用户资料一次插全：

```cpp
// LogicSystem：注册请求多了 confirm（确认密码）与 icon（头像）两个字段
if (pwd != confirm) { root["error"] = ErrorCodes::PasswdErr; ... }
int uid = MysqlMgr::GetInstance()->RegUser(name, email, pwd, icon);

// MysqlDao::RegUserTransaction：全程在同一个连接上、关自动提交
setAutoCommit(false)
  → SELECT 1 FROM user WHERE email = ?     (重复则 rollback / return 0)
  → SELECT 1 FROM user WHERE name  = ?
  → UPDATE user_id SET id = id + 1
  → SELECT id FROM user_id                 (拿到新 uid)
  → INSERT INTO user (uid,name,email,pwd,nick,icon) VALUES (?,?,?,?,?,?)
commit → return newId
```

> 好处：`nick` 默认取 `name`、`icon` 取客户端随机选的头像，注册完就有一条**资料完整**的用户，
> 后面搜索 / 好友列表都不用再补字段。

另外 `CServer1` 新增了 `data.h`（把 `UserInfo` / `ApplyInfo` 从 `MysqlDao.h` 里抽出来），
`StatusServer` 做了同样的抽取 —— 这两个工程的 DAO 头文件因此干净了不少。

### 2.6 数据库换成 `xl`，并导出 `CServer1/xl.sql`

4 个 C++ 工程的 `[Mysql] Schema` 全部从 `xulei` 改成 **`xl`**，并配套导出 `CServer1/xl.sql`（详见 [第 6 节](#6-数据库-cserver1xlsql)）。

---

## 3. 客户端改动

### 3.1 同意好友申请：`AuthenFriend` 对话框（新增）

`applyfriendpage.cpp` 里点「好友申请」条目时 `new AuthenFriend` + `SetApplyInfo(apply_info)`：

| 功能 | 实现 |
|---|---|
| 显示申请人 头像 / 昵称 / 名字 | `SetApplyInfo` 填控件 |
| 给自己起备注（标签可增删） | `ClickedLabel` 提示词 + `FriendLabel` 展示栏 + `eventFilter` 处理 |
| 确定 | `SlotApplySure()` → 发 `ID_AUTH_FRIEND_REQ`（带 `fromuid`=我自己、`touid`=对方、`back`=备注） |
| 取消 | `SlotApplyCancel()` → 直接关窗 |

### 3.2 好友资料页：`FriendInfoPage`（新增）

`ChatDialog` 里多了一页（`ui->friend_info_page`），展示好友头像/昵称/描述，底部「发消息」按钮
通过 `sig_jump_chat_item` 回到聊天页 —— 这是「从好友列表进聊天」和「从消息列表进聊天」的汇合点。

### 3.3 聊天窗口：发消息与收消息

```cpp
// 发送（chatpage.cpp）
QJsonArray text_array;                       // 支持一次发多条
jsonObj["fromuid"] = UserMgr::GetInstance()->GetUid();
jsonObj["touid"]   = _user_info->_uid;
jsonObj["text_array"] = text_array;
emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, jsonData);
emit sig_append_send_chat_msg(txt_msg);      // 先在本地把气泡画出来（乐观更新）

// 接收（chatdialog.cpp）
void ChatDialog::slot_text_chat_msg(std::shared_ptr<TextChatMsg> msg) {
    SetSelectChatItem(msg->_from_uid);       // 顺带把这个会话顶到列表前面
    SetSelectChatPage(msg->_from_uid);
    UpdateChatMsg(msg->_chat_msgs);          // 追加气泡 + 更新 "最后一条消息"
}
```

`TcpMgr` 现在一共 8 个回包处理器，**本次新增 4 个**：

| 消息 | 处理 | 新旧 |
|---|---|---|
| `ID_CHAT_LOGIN_RSP` | 登录成功 → 灌申请/好友列表 → 切到 `ChatDialog` | 旧（本次扩了字段） |
| `ID_SEARCH_USER_RSP` | 搜索结果 → `sig_user_search` | 旧 |
| `ID_ADD_FRIEND_RSP` | 我的申请已送达 → 把该条目置为「已发送」 | 旧（本次扩了逻辑） |
| `ID_NOTIFY_ADD_FRIEND_REQ` | 收到别人的加好友申请 → `sig_friend_apply` → 联系人页加条目 + 红点 | 旧（本次扩了逻辑） |
| **`ID_NOTIFY_AUTH_FRIEND_REQ`** | **对方同意了我的申请 → `sig_add_auth_friend` → 插入好友列表** | 🆕 |
| **`ID_AUTH_FRIEND_RSP`** | **我同意别人 → `sig_auth_rsp` → 对方进好友列表、申请页刷新** | 🆕 |
| **`ID_TEXT_CHAT_MSG_RSP`** | **自己发的消息的回执（服务端已收下）** | 🆕 |
| **`ID_NOTIFY_TEXT_CHAT_MSG_REQ`** | **收到对方发来的文本消息 → `sig_text_chat_msg` → 画气泡** | 🆕 |

### 3.4 `UserMgr` 变成「本地仓储」

| 新增能力 | 说明 |
|---|---|
| `AppendApplyList(QJsonArray)` / `AppendFriendList(QJsonArray)` | 登录回包里两张表一次性灌进来 |
| `GetChatListPerPage()` / `GetConListPerPage()` | 按 `CHAT_COUNT_PER_PAGE = 12` 切片，滚动到底再取下一页 |
| `GetFriendById(uid)` / `CheckFriendById(uid)` | 聊天窗口、申请页判重 |
| `AppendFriendChatMsg(friend_id, msgs)` | 把消息挂到对应好友的 `_chat_msgs`（内存，不落库） |

---

## 4. 相对 day8 的差异（逐文件 MD5 + 文本 diff）

> 先 `Get-FileHash -Algorithm MD5` 全量比对，再对「MD5 不同」的文件做行级 diff，
> 从而把**内容真改了**和**只是重新保存（编码/换行变化）**分开。

### 服务端

| 工程 | 新增 | 真改 | 仅字节变化 | 说明 |
|---|---|---|---|---|
| `ChatServer` | 0 | **10** | 0 | `LogicSystem.cpp` +205、`MysqlDao.cpp` +191、`ChatServiceImpl.cpp` +67、`ChatGrpcClient.cpp` +58、`MysqlMgr.*` +18、`LogicSystem.h` +6、`config.ini`（`Schema=xl`、对端 `Host` 修正） |
| `ChatServer2` | 0 | **10** | 0 | 与节点 1 同步；`config.ini` 另修对端段名（原写成 `[chatserver2]` 却配 `chatserver1`） |
| `CServer1` | **2**（`data.h`、`xl.sql`） | **6** | 16 | `MysqlDao.cpp` +146（事务注册）、`LogicSystem.cpp` +26（`confirm`/`icon`）、`MysqlMgr.*`、`config.ini`；那 16 个「仅字节变化」是重新保存导致的，逻辑一字未动 |
| `StatusServer` | **1**（`data.h`） | **6** | 0 | `UserInfo` 抽到 `data.h`、`unique_ptr` 统一写法、`vcxproj` 登记新头文件、`Schema=xl` |
| `VarifyServer` | 0 | **0** | 0 | 代码完全没动 |

### 客户端（vs `day8`）

| 类别 | 文件 |
|---|---|
| **新增（6 源文件 + 2 工程文件 + 1 资源脚本）** | `authenfriend.cpp/.h/.ui`、`friendinfopage.cpp/.h/.ui`、`llfcchat.vcxproj`(+`.filters`)、`llfcchat_resource.rc` |
| **改动（27）** | `chatdialog.cpp`（+515/−4，最大头：发消息/收消息/分页/资料页跳转）、`usermgr.cpp`(+204)/`usermgr.h`(+26)、`tcpmgr.cpp`(+150)/`tcpmgr.h`(+2)、`chatpage.cpp`(+112)、`contactuserlist.cpp`(+110)、`userdata.h`(+56，新增 `AuthInfo`/`AuthRsp`/`FriendInfo`/`TextChatData`/`TextChatMsg`)、`chatuserwid.cpp`(+45)、`chatuserlist.cpp`(+22)、`chatview.cpp`(+17)、`registerdialog.cpp`(+17，注册时随机选头像)、`applyfriendpage.cpp`(+14)、`searchlist.cpp`、`applyfrienditem.cpp`、`global.h`(+1，`CHAT_COUNT_PER_PAGE`)、`llfcchat.pro`(+9)、`style/stylesheet.qss`(+98) |

> `llfcchat.vcxproj` / `llfcchat_resource.rc` 是 Qt Creator 生成的 **MSVC 工程文件**，本次一起归档，
> 方便直接用 VS2022 打开客户端；`llfcchat.qtvscr`（1.6 MB 的 Qt Creator 会话恢复文件）已排除，没有提交。

---

## 5. 端口与配置总表

| 服务 / 端口 | 值 | 配置位置 |
|---|---|---|
| HTTP 网关 | 8080 | `CServer1/config.ini` `[GateServer]` |
| VarifyServer gRPC | 50051 | 各 `config.ini` 的 `[VarifyServer]` |
| StatusServer gRPC | 50052 | `StatusServer/config.ini` `[StatusServer]` |
| ChatServer 客户端 TCP | 8090 | `ChatServer/config.ini` `[SelfServer] Port` |
| ChatServer 对端 gRPC | 50055 | `ChatServer/config.ini` `[SelfServer] RPCPort` |
| ChatServer2 客户端 TCP | 8091 | `ChatServer2/config.ini` `[SelfServer] Port` |
| ChatServer2 对端 gRPC | 50056 | `ChatServer2/config.ini` `[SelfServer] RPCPort` |
| MySQL | 3308 | `[Mysql]`，用户 `llfc`，库名 **`xl`** |
| Redis | 6380 | `[Redis]`，密码 `123456` |

节点互认（`ChatServer` 的写法，`ChatServer2` 镜像对称）：

```ini
[SelfServer]
Name = chatserver1
Port = 8090
RPCPort = 50055

[PeerServer]          ; ← 本次改成这个段名（day8 代码读的是 [ProtoServer]，读不到）
Server = chatserver2  ; 对端名字（多个用逗号分隔）

[chatserver2]         ; 以对端名字命名的段
Name = chatserver2
Host = 127.0.0.1      ; 本次从 127.0.0.127 修正
Port = 50056
```

`StatusServer` 侧按连接数挑节点：

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

> `getChatServer()` 对每个节点 `HGET logincount <name>`，**取连接数最小的那个**返回给网关。

---

## 6. 数据库 `CServer1/xl.sql`

本次终于把可用的 schema 导出来了（Navicat 导出，MySQL 9.4，库名 `xl`）：

| 对象 | 说明 |
|---|---|
| 表 `user` | **9 列**：`id / uid / name / email / pwd / nick / desc / sex / icon`。含 38 行测试用户（`uid` 从 1002 到 1066） |
| 表 `friend` | 好友关系，**双向两条记录**（`self_id` + `friend_id` + `back` 备注），唯一索引 `(self_id, friend_id)` |
| 表 `friend_apply` | 好友申请（`from_uid / to_uid / status`，`status=0` 待处理、`=1` 已通过），唯一索引 `(from_uid, to_uid)` |
| 表 `user_id` | uid 自增计数表（当前 `id = 1066`） |
| 存储过程 `reg_user` | **早期注册用的**（分配 uid + 插 user）。本次代码已改成显式事务，**不再依赖它**，留着只为对照 |

> ⚠️ 这份 SQL 来自**本地学习环境**：`user` 表里有真实测试邮箱与**明文密码**（课程环境惯例）。
> 公开仓库仅作学习归档，**请勿在正式环境复用，也不要往里面填自己的真实密码**。

与 day8 的对比：day8 的 `xulei.sql` 里 `user` 只有 5 列（`id/uid/name/email/pwd`），
导致「按名字搜人会报 SQL 异常」—— 那个已知问题**在本次的 `xl` 库里已经不存在了**（`nick/desc/sex/icon` 都在）。

恢复步骤：

```bash
mysql -h127.0.0.1 -P3308 -ullfc -p123456 < CServer1/xl.sql
```

---

## 7. 已知问题

> 这些是**归档时刻的代码原貌**，本分支只做归档、未作任何修改，留给第二季补齐。

| # | 问题 | 影响 |
|---|---|---|
| 1 | **文本消息不落库**：`DealChatTextMsg` 只做转发，整个库里也没有 `chat_message` / `private_chat` 之类的表 | 重启服务、或对方没在线，消息就**永久丢了**；客户端 `_chat_msgs` 也只在内存里 |
| 2 | **发送方永远收到"成功"**：`rtvalue["error"]` 在出发前就置成 `Success`，`ChatGrpcClient::NotifyTextChatMsg` 的返回值被忽略 | 对端不在线 / 节点 RPC 失败时，发送方界面上消息正常显示，实际没送到 |
| 3 | `AuthFriendApply` 里 `bool b_ip = RedisMgr::Get(uip_<touid>)` 失败时是**空 `if` 块**，继续往下走 gRPC | 对方从没登录过（Redis 无 `uip_` 键）时，会拿空字符串当节点名去发 RPC |
| 4 | 好友申请列表只在登录时取前 **10 条**（`GetApplyList(..., 0, 10)`） | 申请多了看不到，也没有「已读/未读」标记，客户端红点是 `contactuserlist` 本地状态 |
| 5 | 好友列表 / 申请列表**没有增量接口**，全靠登录回包一次性下发 | 新加好友后，只有当前登录会话能立刻看到；重登才同步 |
| 6 | `StatusServer` 启动时**不再**把 `logincount` 清零（day8 起把两段 `HSet` 注释掉了） | 节点重启后计数会残留，负载分派可能偏向旧数据 |
| 7 | 服务端部分源文件里**中文注释编码混杂**（同一个文件里 GBK 与 UTF-8 字节都有） | GitHub 网页上看中文注释会显示成乱码，**不影响 VS 编译**（本机区域设置下能过） |
| 8 | `message.proto` 有 **4 份拷贝**（`CServer1` / `StatusServer` / `ChatServer` / `ChatServer2`） | 改 proto 必须四份一起改、一起重新生成，漏一个就字段对不上 |

---

## 8. 环境要求

| 组件 | 版本 / 说明 |
|---|---|
| Qt | 6.8.3 MinGW（客户端 `llfcchat`；也可用 `llfcchat.vcxproj` 走 VS2022） |
| Visual Studio | 2022（MSVC v143），x64 + **Debug** 配置（4 个 C++ 服务端） |
| gRPC / protobuf | 1.34.0 / 3.13.0（各工程用 `PropertySheet1.props` 统一引依赖） |
| MySQL | 9.4，端口 3308，库名 `xl` |
| Redis | 端口 6380，密码 `123456` |
| Node.js | 跑 `VarifyServer` 用；`nodemailer` 发邮件、`ioredis` 连 Redis |

> `mysqlcppconn-9-vs14.dll` / `mysqlcppconn8-2-vs14.dll`（各约 15 MB / 9 MB）是**运行时依赖**，
> 为了不把仓库撑大**没有提交**，请从本机 MySQL Connector/C++ 安装目录自行拷到 exe 同级目录。

---

## 9. 编译 / 运行

```bash
# 0) 起 Redis（6380，密码 123456）与 MySQL（3308）
mysql -h127.0.0.1 -P3308 -ullfc -p123456 < CServer1/xl.sql

# 1) 按顺序启动：VarifyServer → StatusServer → ChatServer → ChatServer2 → CServer1
#    4 个 C++ 工程用 VS2022 打开各自的 .sln，选 x64 / Debug 生成
#    客户端 llfcchat 用 Qt Creator 打开 llfcchat.pro，或直接用 llfcchat.vcxproj

# 2) VarifyServer（Node.js）
cd VarifyServer
npm install
cp config.example.json config.json   # 然后填自己的邮箱授权码与 Redis 密码
node server.js
```

`config.ini` 会被各工程属性表的**后置事件**拷到 `x64\Debug\`，
**改完 `config.ini` 必须重新生成一次才生效**（`ConfigMgr` 读的是 exe 同级目录那份）。

---

## 10. ⚠️ 关于配置文件

- `VarifyServer/config.json` **含真实的 163 邮箱 SMTP 授权码与 Redis 密码**，
  已在 `.gitignore` 中排除，**不在本仓库里**。仓库只提供模板 `VarifyServer/config.example.json`。
  如果你的仓库历史里曾出现过 `config.json`，请到邮箱服务商后台**重新生成授权码**。
- 各处 `config.ini` 里的 `User=llfc` / `Passwd=123456` 是**本地学习环境的占位密码**，随源码提交。
- ⚠️ `VarifyServer/server.js` 的 `mailOptions.from` 里**硬编码了发件邮箱地址**（`15869988660@163.com`），
  这是源码原样，**没有做脱敏**（授权码本身只在被忽略的 `config.json` 里）。介意的话可以把它改成占位符。
- `CServer1/xl.sql` 含真实测试邮箱与明文密码（见第 6 节说明）。
- 本仓库是**公开仓库**，往配置里填真实地址或密码前请先确认已脱敏。

---

## 11. 说明与约定

1. **不提交构建产物**：`.vs/`、`x64/`、`项目名/项目名/`（VS 嵌套中间目录）、`*.vcxproj.user`、
   `bin/`（Qt 输出目录）、`*.qtvscr`（Qt Creator 会话文件）、`*.dll`、`*.exe`、`*.pdb`、`*.bak` 等全部忽略。
2. **protobuf 生成物保留**：`message.pb.*` / `message.grpc.pb.*` 已提交，
   克隆下来不必自备 protoc 与 grpc_cpp_plugin 就能编译。
3. **两个聊天节点同源**：`ChatServer` 与 `ChatServer2` 源码内容基本一致，只靠 `config.ini` 区分身份
   （`Name` / `Port` / `RPCPort` / `[PeerServer]`）—— 这是刻意的：同一份代码换个配置就是另一个节点。
4. **`.sql` 放在 `CServer1/` 而不是根目录**：本次按实际存放位置归档（用户的原话：「sql 文件在 `D:\IT\cpp\CServer1` 路径下」），
   day8 根目录那份 `xulei.sql` 不再重复提供。
