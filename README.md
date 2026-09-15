# code

个人 C++ / Qt 练习代码汇总仓库。按学习进度归档，每个阶段一个文件夹。

本仓库**只提交源码、工程文件与资源**，编译产物与缓存已在 `.gitignore` 中全部排除。

---

## day1 — 账号注册的验证码功能 ✅ 已完成

`day1/` 记录了 IM 项目第一天的成果：**用户注册流程中的邮箱验证码功能已打通**。

### 三个模块

| 模块 | 技术栈 | 职责 |
|---|---|---|
| [`day1/CServer1`](day1/CServer1) | C++ / cpp-httplib / gRPC / protobuf | **网关服务器 GateServer**。对外提供 HTTP 接口，接收 Qt 客户端发来的注册请求；对内作为 gRPC 客户端，调用验证码服务 |
| [`day1/VarifyServer`](day1/VarifyServer) | Node.js / @grpc/grpc-js / nodemailer / Redis / MySQL | **验证码服务**。生成随机验证码，通过 163 邮箱发送邮件；验证码写入 Redis 并设置过期时间（key 前缀 `code_`） |
| [`day1/llfcchat`](day1/llfcchat) | Qt 6.8 / qmake / QSS | **IM 客户端**。登录界面与注册界面；注册页点击"获取验证码"后向网关发起 HTTP 请求 |

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
      ├── 生成随机验证码
      ├── Redis 写入 code_<email>，设置过期时间
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

## 环境要求

| 组件 | 版本 / 说明 |
|---|---|
| Visual Studio | 2022（v143 工具集），用于编译 CServer1 |
| vcpkg | 依赖：cpp-httplib、protobuf、grpc、jsoncpp |
| Qt | 6.8.3 MinGW 64-bit（llfcchat） |
| Node.js | 18+，用于 VarifyServer |
| Redis | 本地 6379，验证码缓存 |
| MySQL | 本地 3306，用户表 |
| 163 邮箱 | 需开启 SMTP 并获取授权码 |

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
Port = 8080
[VarifyServer]
Port = 50051
```

### VarifyServer（验证码服务）

```bash
cd day1/VarifyServer
npm install          # 依赖不在仓库里，需要自行安装
node server.js
```

**首次运行前必须创建 `config.json`**（见下方说明）。

### llfcchat（Qt 客户端）

用 Qt Creator 打开 `llfcchat.pro`，选择 Desktop Qt 6.8.3 MinGW 64-bit 套件，直接构建运行。

---

## ⚠️ 关于 `config.json`（重要）

`day1/VarifyServer/config.json` **没有提交到仓库**，因为它包含真实凭据：

- 163 邮箱账号与 **SMTP 授权码**
- Redis 连接密码

仓库里提供的是模板 [`day1/VarifyServer/config.example.json`](day1/VarifyServer/config.example.json)。首次使用请复制并填写自己的信息：

```bash
cd day1/VarifyServer
cp config.example.json config.json
# 然后编辑 config.json，填入自己的邮箱、授权码、Redis 密码
```

---

## 说明与约定

1. **不提交构建产物**：`.vs/`、`x64/`、`build/`、`node_modules/`、`moc_*`、`ui_*.h`、`*.pdb`、`*.ilk` 等全部忽略。
   （原始工程目录含约 2.5 GB 的 VS 缓存，本仓库只保留约 1 MB 的源码。）
2. **protobuf 生成文件是提交的**：`message.pb.h/.cc`、`message.grpc.pb.h/.cc` 由 `protoc` 从 `message.proto` 生成。
   保留它们是为了让仓库克隆后可直接编译（Windows 上配置 protoc + grpc_cpp_plugin 较繁琐）。
   如果你更倾向"只提交 .proto"，把它们加进 `.gitignore` 并补充生成命令即可。
3. **`*.vcxproj.user`、`*.vcxproj.bak` 未提交**：前者是个人环境配置，后者是备份文件。
