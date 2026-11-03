# 恋恋风辰 IM — day10 分支（Linux 迁移版）

> **关于本仓库的组织方式**：按「一天一个分支」归档，跟着 B 站恋恋风辰的 IM 项目教程（第一季）推进。
>
> | 分支 | 内容 |
> |---|---|
> | `master` | 第 1 ~ 4 天的早期归档 |
> | `day5` ~ `day9` | Windows 阶段：day9 = 第一季完结（注册 → 登录 → 加好友 → 双节点文本聊天） |
> | **`day10`（本分支）** | **整体迁移到 Linux (WSL2 + Ubuntu 24.04)，编译体系改为 CMake/qmake，修复一批跨平台 bug，UI 全面定制** |
>
> 在 GitHub 页面左上角的**分支下拉框**里切换即可对比查看。

这是一个**仿微信的即时通讯系统**：Qt 6 桌面客户端 + 5 个服务端（HTTP 网关 / 状态服务 / 双聊天节点 / 验证码服务），
后端为 C++ (Boost.Asio + gRPC + MySQL + Redis)，覆盖注册、登录、邮件验证码、搜索/添加好友、
好友认证、双节点互发文本消息的完整链路。

---

## 1. 系统拓扑

```
                        ┌──────────────────────┐
   Qt 客户端 ──HTTP────▶ │  GateServer 网关 :8080│   （day9 及之前叫 CServer1）
                        └───────┬──────────────┘
                                │ gRPC
                    ┌───────────▼───────────┐        ┌──────────────────┐
                    │ StatusServer  :50052  │───────▶│ VarifyServer     │
                    │  · 发/校验 token      │  gRPC  │ (Node) :50051    │
                    │  · 按负载挑节点       │        └──────────────────┘
                    └───────────┬───────────┘
        ┌───────────────────────┴───────────────────────┐
┌───────▼───────────────┐    gRPC 50055 ⇄ 50056   ┌───────▼───────────────┐
│ ChatServer  (node1)   │◀───────────────────────▶│ ChatServer2 (node2)  │
│  · 客户端 TCP  :8090  │   跨节点转发消息/通知     │  · 客户端 TCP  :8091  │
└───────┬───────────────┘                          └───────┬───────────────┘
        └───────────────┬──────────────────────────────────┘
              ┌─────────▼─────────┐   ┌──────────────┐
              │ Redis     :6380   │   │ MySQL  :3306 │
              │ · logincount 负载 │   │ 库名 xl      │
              │ · uip_<uid> 路由  │   │ 4 表        │
              │ · ubaseinfo_ 缓存 │   └──────────────┘
              └───────────────────┘
```

- **路由关键点**：Redis 键 `uip_<uid>` 存「用户 → 所在节点名」，聊天节点比对 `[SelfServer] Name`
  决定本机直投还是走 gRPC 转发到对端节点。
- **day10 起 ChatServer2 不再复制源码**：`ChatServer2/CMakeLists.txt` 直接编译 `../ChatServer` 的共享源码，
  只靠各自的 `config.ini` 区分身份（同 day9 的设计意图，实现落到实处）。

## 2. 目录结构

| 目录 | 内容 |
|---|---|
| `llfcchat/` | Qt 6 桌面客户端（qmake 工程 `llfcchat.pro`） |
| `GateServer/` | HTTP 网关 :8080（注册/登录/找回密码/分派节点）+ 数据库脚本 `xl.sql` |
| `StatusServer/` | 状态服务 :50052（token、按连接数挑节点） |
| `ChatServer/` | 聊天节点 1（:8090 TCP / :50055 gRPC） |
| `ChatServer2/` | 聊天节点 2（:8091 / :50056），共享 ChatServer 源码 |
| `VarifyServer/` | Node.js 验证码服务 :50051（邮件 + Redis） |
| `CMakeLists.txt` | 服务端总控 CMake（gate/status/chat×2 一次配全） |
| `run_all.sh` | 服务端一键 start / stop / status |
| `llfcchat/chat-cn.sh` | 客户端启动脚本（自动拉起 fcitx5 中文输入法环境） |

## 3. Linux 下编译与运行

### 3.1 环境依赖（Ubuntu 24.04 / WSL2 实测）

| 组件 | 说明 |
|---|---|
| g++ / cmake | C++17，服务端编译 |
| Qt 6 | 系统包 `qt6-base-dev` 等；客户端用 `qmake6` + `make` 构建 |
| Boost | `libboost-all-dev`（至少 filesystem） |
| gRPC / protobuf | 系统包 `libgrpc++-dev protobuf-compiler-grpc` |
| hiredis | `libhiredis-dev` |
| jsoncpp / mysqlcppconn | 见根 `CMakeLists.txt` 的 `USER_PREFIX`（无 root 时装到用户前缀） |
| MySQL 8 | 端口 3306，库 `xl`，用户 `llfc/123456` |
| Redis | 端口 6380，密码 `123456`（`run_all.sh` 会自动拉起） |
| Node.js 20 + nodemailer + ioredis | 跑 VarifyServer |

### 3.2 初始化数据库

```bash
mysql -h127.0.0.1 -P3306 -ullfc -p123456 < GateServer/xl.sql
```

> ⚠️ `xl.sql` 含本地学习环境的测试邮箱与明文密码（课程惯例），请勿用于正式环境。

### 3.3 编译

```bash
# 服务端（4 个一起）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# 客户端
cd llfcchat
qmake6 llfcchat.pro
make -j$(nproc)
```

### 3.4 启动

```bash
./run_all.sh start        # Redis + Varify → Status → Chat1 → Chat2 → Gate，日志在 logs/
./run_all.sh status       # 看端口监听与进程
./run_all.sh stop

# 客户端（带中文输入法环境）
cd llfcchat && ./chat-cn.sh
```

测试账号：`xl`（uid 1064，邮箱 745899278@qq.com，密码 123456）等，见 `xl.sql`。
注册新用户需要 VarifyServer 的邮箱验证码（`VarifyServer/config.json` 已被 .gitignore 排除，
模板见 `config.example.json`，需自行填入 SMTP 授权码）。

### 3.5 踩坑备忘（改代码后必须做的事）

| 改了什么 | 必须做 |
|---|---|
| 客户端代码 / qss（打包在 qrc） | 重编客户端并**重启所有客户端实例** |
| `ChatServer` 源码 | **两个节点一起**重编重启（day10 起共享源码，一条 `cmake --build` 即可） |
| MySQL 数据 | 清掉对应 Redis 缓存键（`ubaseinfo_<uid>` / `nameinfo_<name>`）再重新登录，否则读旧缓存 |

## 4. day10 相对 day9 的主要变化

### 4.1 迁移与编译

- `CServer1` 更名 **`GateServer`**；全部服务端改用 **CMake**（根 `CMakeLists.txt` 统一管理，
  protoc/gRPC 在构建期生成，仓库不再提交 `message.pb.*` 生成物）
- `ChatServer2` 改为**共享 ChatServer 源码**（原 day9 是整目录复制，改一处忘另一处必出 bug）
- 客户端迁移到 Linux：`qmake6 + make`；`.pro` 中 Windows 专属的 copy 段加 `win32{}` 保护

### 4.2 跨平台 bug 修复（MSVC 能跑、GCC 下必炸/行为不同的）

| Bug | 修复 |
|---|---|
| `ConfigMgr.h` 的 `SectionInfo::operator=` 缺 `return *this`（UB，GCC 下 SIGILL 崩溃） | 补 return |
| `GetBaseInfo` Redis 命中分支缺 return | 补 return |
| 中文乱码 | 统一 UTF-8 + 字体 fallback（Microsoft YaHei → 文泉驿微米黑） |
| 文本气泡换行：Linux 回退字体度量差异导致提前换行 | `textbubble.cpp` 最大宽度 +15px 余量 |
| Linux 原生样式下登录/注册页粗边框、蓝色聚焦圈 | qss 扁平化输入框与按钮 |

### 4.3 功能/逻辑 bug 修复

| Bug | 修复 |
|---|---|
| 侧边栏红点永远无法熄灭：`StateWidget::ShowRedPoint(bool)` 忽略参数恒 `setVisible(true)`（教程 bug） | 按参数显隐 |
| 红点规格修正：**只有申请接收方（b）亮红点**（侧边栏联系人图标 + 新的朋友），同意后立即熄灭，申请方（a）永不亮 | `chatdialog.cpp` 相应槽函数调整 |
| 同意好友后联系人列表头像随机 | 优先用服务端下发的真实 icon，丢弃随机回退 |
| 搜索无头像：`FindSuccessDlg::SetSearchInfo` 从未给 head_lb 赋值 | 补头像加载 |
| 搜索偶发无头像：ChatServer2 未重编导致响应缺 icon | 见 4.1 共享源码重构（根治） |
| 给非好友发消息对方客户端段错误（`GetFriendById` 空指针解引用） | chatdialog / chatuserwid 双重判空 |
| 聊天列表时间标签恒为占位文字 | 写入真实时间 hh:mm |
| 切换界面 use-after-free：`setCentralWidget` 会 delete 旧界面、随后又访问旧指针 | 统一走 `SetCenterWidget`（hide + deleteLater） |

### 4.4 UI 定制

- **无边框窗口 + 自定义标题栏**：图标 / 标题 / 最小化-最大化-关闭 / 按住拖动（WSLg 原生标题栏又丑、
  点关闭还要绕 Weston→XWayland 协议导致延迟，全部绕开）
- 窗口图标改用 PNG（Linux 下 Qt 缺 qico 插件，`.ico` 显示成空白块）
- 侧边栏图标置顶（对齐微信桌面端）、标题 xulei666、专属头像（xl / ly）、聊天列表头像批量替换
- 头像统一走 `LoadAvatarPixmap` 缓存（原图只解码一次、缩放结果按 路径@尺寸 缓存），
  过大的素材图已降像素，解决登录后滚动列表卡顿
- 移除未实现的「接收」按钮

## 5. 端口与配置总表

| 服务 / 端口 | 值 | 配置位置 |
|---|---|---|
| HTTP 网关 | 8080 | `GateServer/config.ini` `[GateServer]` |
| VarifyServer gRPC | 50051 | 各 `config.ini` `[VarifyServer]` |
| StatusServer gRPC | 50052 | `StatusServer/config.ini` `[StatusServer]` |
| ChatServer TCP / gRPC | 8090 / 50055 | `ChatServer/config.ini` `[SelfServer]` |
| ChatServer2 TCP / gRPC | 8091 / 50056 | `ChatServer2/config.ini` `[SelfServer]` |
| MySQL | 3306（day9 是 3308） | `[Mysql]`，用户 `llfc`，库名 `xl` |
| Redis | 6380 | `[Redis]`，密码 `123456` |

> `config.ini` 由 CMake 后置拷到各可执行文件同级目录，**改完必须重编一次才生效**。

## 6. 已知问题 / TODO

| # | 问题 | 影响 |
|---|---|---|
| 1 | **文本消息不落库**（无 chat_message 表，客户端 _chat_msgs 仅内存） | 对方离线 / 服务重启，消息丢失 |
| 2 | 发送方永远收到"成功"，对端不在线也无感知 | 假送达 |
| 3 | **MysqlDao 存在事务泄漏**（部分失败路径不 commit 不 rollback） | 偶发 MySQL 死锁，需重启服务端释放，**待修复** |
| 4 | 好友申请列表只取前 10 条；好友/申请列表无增量接口，重登才同步 | 多申请看不全 |
| 5 | `message.proto` 仍是 4 份拷贝（Gate/Status/ChatServer 各一份） | 改 proto 要四处同步 |
| 6 | StatusServer 启动不清零 `logincount` | 节点重启后负载分派可能偏向旧数据 |

## 7. 说明与约定

- 只提交源码、工程文件与资源；`build/`、`bin/`、`logs/`、`*.o`、`Makefile`、`.qmake.stash` 等全部忽略
- `VarifyServer/config.json` 含邮箱 SMTP 授权码，**不入库**，仓库只提供 `config.example.json` 模板
- 各处 `config.ini` 的数据库/Redis 密码是本地学习环境占位值，请勿填真实凭据
- Windows 工程文件（`.sln`/`.vcxproj`）随源码保留，day9 分支的 Windows 编译方式仍然可用
