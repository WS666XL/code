# code — day6 分支

> **关于本仓库的组织方式**：按「一天一个分支」归档。
> 本 `day6` 分支里**只有第 6 天**的代码 —— 本次只做了一个工程：Qt 客户端 `llfcchat`，平铺在根目录。
> 第 1 ~ 4 天的代码在 **`master` 分支**（按 `day1/` ~ `day4/` 文件夹归档）；
> 第 5 天（4 个服务端 + 客户端）在 **`day5` 分支**。
> 在 GitHub 页面左上角的**分支下拉框**里切换 `master` / `day5` / `day6` 即可对比查看。

本仓库**只提交源码、工程文件与资源**，编译产物与缓存已在 `.gitignore` 中全部排除。

---

## 本次完成

本次**只动 Qt 客户端**，服务端一行没改（服务端最新状态见 `day5` 分支）。客户端这次一下补齐了三块界面：

| # | 内容 | 说明 |
|---|---|---|
| 1 | **聊天消息界面** | 真正的聊天气泡：文字气泡 + 图片气泡，按角色（我 / 对方）决定左右；支持回车发送、把文件拖进输入框 |
| 2 | **好友搜索与添加申请** | 搜索用户名 → 结果列表 → 弹出对方资料 → 填备注名、给好友打标签 → 发送申请 |
| 3 | **联系人列表与好友申请列表** | 左侧联系人列表（含分组提示、添加好友入口、红点），以及独立的「好友申请」页 |

---

## 工程（本次只 1 个）

| 工程 | 技术栈 | 职责 | 本次状态 |
|---|---|---|---|
| [`llfcchat`](llfcchat) | Qt 6 / qmake / QSS | **IM 客户端**。登录 / 注册 / 找回密码 + 聊天界面 + 好友搜索 / 申请 / 联系人 | 🆕 新增 55 个文件，修改 21 个 |

与 `day5` 分支里的 `llfcchat` 相比：**106 个文件 → 161 个文件**（构建产物已排除，合计约 4.8 MB）。

---

## 1. 聊天消息界面

| 文件 | 作用 |
|---|---|
| `chatpage.h/.cpp/.ui` | 聊天页：上面是消息列表，下面是输入区 + 发送按钮 |
| `chatview.h/.cpp` | 消息列表容器（`QScrollArea` 派生）。内部垂直布局末尾塞了一个高度 100000 的空白控件当「弹簧」，让气泡总是靠上排列；新消息插入后自动滚到底 |
| `chatitembase.h/.cpp` | **一条消息的骨架**：头像 + 用户名 + 气泡占位。按 `ChatRole`（`Self` 自己发的 / `Other` 对方发的）决定头像和名字放左还是放右 |
| `bubbleframe.h/.cpp` | 气泡基类，只负责画「带小尖角的气泡外形」，内容交给子类 |
| `textbubble.h/.cpp` | 文字气泡。用 `QTextEdit` 而不是 `QLabel`，因为要按文字行数自动调整气泡高度 |
| `picturebubble.h/.cpp` | 图片气泡 |
| `messagetextedit.h/.cpp` | 聊天输入框（`QTextEdit` 派生）。支持拖拽文件进入、回车发送、把文件变成带图标的卡片 |

发送流程（`ChatPage::on_send_btn_clicked`）：

```
输入框 MessageTextEdit
   │  getMsgList()  →  QVector<MsgInfo>（每条带 msgFlag：text / image / file）
   ▼
逐条生成 ChatItemBase（骨架）
   │  setUserName() / setUserIcon()
   ▼
按 msgFlag 造气泡：TextBubble（文字） 或 PictureBubble（图片）
   │  pChatItem->setWidget(pBubble)   把占位气泡换成真正的气泡
   ▼
ChatView::appendChatItem()  →  自动滚到底部
```

---

## 2. 好友搜索与添加申请

| 文件 | 作用 |
|---|---|
| `searchlist.h/.cpp` | 搜索结果显示列表（鼠标悬停才显示滚动条） |
| `findsuccessdlg.h/.cpp/.ui` | 搜到用户后的「资料 + 添加」弹窗 |
| `applyfriend.h/.cpp/.ui` | **添加好友申请对话框**：填备注名、给好友打标签（可点已有标签，也能自己输入新标签） |
| `friendlabel.h/.cpp/.ui` | 申请对话框里**已选标签的小方块**（带 × 可删除） |
| `clickedoncelabel.h/.cpp` | 一次性点击的标签（给「更多」按钮用） |
| `loadingdlg.h/.cpp/.ui` | 「加载中」蒙层弹窗（居中一个 loading.gif） |

标签这块是本次最绕的部分：标签在 `gridWidget` 里**手工 `move()` 绝对定位**（不用布局管理器），
换行、让输入框 `lb_ed` 跟着标签走、删除后重排，全都要自己算坐标。

> 顺带记两个真实踩过的坑（不影响功能，但排查花了不少时间）：
>
> 1. `friendlabel.ui` 里 `FriendLabel` **控件自身的 styleSheet** 一旦写成白色，就会盖掉全局 QSS 里的绿色高亮。
>    Qt 的层叠规则是「**控件自身的样式表永远优先于继承来的（含 QApplication 的）样式表，与选择器特异性无关**」。
> 2. `chatdialog.ui` 里 `QStackedWidget` 的 `currentIndex` 会被 Qt Designer 写成「你当前正在编辑的那一页」。
>    Designer 里点过申请页，`currentIndex` 就变成 1，程序一启动就显示申请页 ——
>    必须在构造函数里显式 `setCurrentWidget(chat_page)` 兜住，`side_chat_lb->SetSelected(true)` 只改图标外观、不换页。

---

## 3. 联系人列表与好友申请列表

| 文件 | 作用 |
|---|---|
| `contactuserlist.h/.cpp` | 联系人列表控件，含「新的朋友」入口与分组提示，支持红点 |
| `conuseritem.h/.cpp/.ui` | 单个联系人条目（头像 / 昵称 / 个性签名） |
| `grouptipitem.h/.cpp/.ui` | 分组标题条（如「新的朋友」「我的好友」） |
| `adduseritem.h/.cpp/.ui` | 「添加好友」提示条目 |
| `applyfriendpage.h/.cpp/.ui` | **好友申请页**（`stackedWidget` 的第 2 页） |
| `applyfrienditem.h/.cpp/.ui` | 一条好友申请（含「添加」按钮） |
| `applyfriendlist.h/.cpp` | 申请列表容器 |

---

## 4. 网络层与数据模型的扩展

| 文件 | 本次改动 |
|---|---|
| `global.h` | 消息 id 从 `1006` 扩到 **`1015`**：搜索用户 `1007/1008`、添加好友申请 `1009/1010/1011`、认证好友 `1013/1014/1015`；`ListItemType` 增加 `LINE_ITEM` / `APPLY_FRIEND_ITEM`；新增 `MIN_APPLY_LABEL_ED_LEN` / `add_prefix` / `tip_offset`；把测试用的 `strs` / `heads` / `names` 从 `chatdialog.cpp` 提到这里 |
| `tcpmgr.h/.cpp` | 新增 3 个信号：`sig_friend_apply` / `sig_add_auth_friend` / `sig_auth_rsp` |
| `userdata.h` | 数据模型大幅扩充：`AddFriendApply` / `ApplyInfo` / `AuthInfo` / `AuthRsp` / `FriendInfo` / `UserInfo` |
| `usermgr.h/.cpp` | 新增 `GetUid()` / `GetName()` / `GetApplyList()`，缓存好友申请列表 |
| `chatdialog.h/.cpp/.ui` | 主界面接上联系人列表、好友申请页、搜索列表，新增三种模式（搜索 / 聊天 / 联系人）之间的切换 |
| `style/stylesheet.qss` | 新增申请对话框、标签、联系人列表等样式（约 +200 行） |
| `llfcchat.pro` | 新增 24 处 `SOURCES` / `HEADERS` / `FORMS` 条目 |
| `chatuserwid.*` / `listitembase.*` / `clickedbtn.*` / `clickedlabel.*` / `chatuserlist.h` / `customizeedit.h` | 配合新界面调整 |

> 说明：其中 `timberbtn.h`、`customizeedit.h`、`listitembase.h` 等文件本次只有**注释**变化 ——
> 这一轮给一批头文件补齐了中文注释，代码逻辑未改。

---

## 与 day5 客户端的关系

客户端连服务端的链路（登录 → 拿 token → 连 ChatServer）在 day5 已经打通，本次没有改动；
本次只在其上**加界面**：

```
day5 已有：LoginDialog → HTTP 登录 → TcpMgr 长连接 → ChatDialog
day6 新增：ChatDialog 里塞进了 三个列表 + 两个页面
              ├─ search_list     搜索结果
              ├─ chat_user_list  会话列表
              ├─ con_user_list   联系人列表 ──▶ 好友申请页 ApplyFriendPage
              └─ stackedWidget ──┬─ page 0  chat_page        聊天页（文字/图片气泡）
                                 └─ page 1  friend_apply_page 好友申请页
```

---

## 环境要求

| 组件 | 版本 / 说明 |
|---|---|
| Qt | 6.8.3（MinGW 64-bit 套件），用 Qt Creator 打开 |
| 编译器 | MinGW 64-bit（随 Qt 安装） |
| 后端服务 | 本地要跑 day5 分支里的四个服务端，客户端才能完整跑通登录 |

---

## 编译 / 运行

```bash
# 用 Qt Creator 打开：
llfcchat/llfcchat.pro
# 选择套件：Desktop Qt 6.8.x MinGW 64-bit
# 直接构建运行
```

`config.ini` 与 `static/` 目录会被 `.pro` 里的 `QMAKE_POST_LINK` 自动拷到 exe 同目录，
所以**改了 `config.ini` 后需要重新构建才生效**。

### 依赖的后端（见 `day5` 分支）

| 服务 | 端口 |
|---|---|
| CServer1（HTTP 网关） | 8080 |
| VarifyServer（gRPC） | 50051 |
| StatusServer（gRPC） | 50052 |
| ChatServer（TCP 长连接） | 8090 |

`llfcchat/config.ini` 当前内容：

```ini
[GateServer]
host=localhost
port=8080
```

---

## ⚠️ 关于配置文件

- 本工程只有 `config.ini`，内容是本机 `localhost:8080`，**不含任何密钥**，随源码提交。
- `VarifyServer/config.json`（内含 163 邮箱 SMTP 授权码、Redis 密码）**不在这条分支上**；
  它在 `day5` 分支里也是被排除的，仓库只放 `config.example.json` 模板。
- 本仓库是**公开仓库**，往 `config.ini` 里填真实服务器地址或密码前，请先确认已脱敏。

---

## 说明与约定

1. **不提交构建产物**：`.qtcreator/`、`build/`、`ui_*.h`、`moc_*`、`qrc_*`、`Makefile`、`*.pro.user` 等全部忽略。
2. **本次没有服务端改动**：本分支只含 `llfcchat` 一个工程。要看服务端请切到 `day5` 分支。
3. `timberbtn.cpp/.h` 是历史遗留的空壳文件（未被 `.pro` 收录），保留但**不参与编译**。
   真正在用的是 `timerbtn.cpp/.h`（「获取验证码」的倒计时按钮）。
