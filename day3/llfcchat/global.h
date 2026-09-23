#ifndef GLOBAL_H
#define GLOBAL_H
#include<QWidget>
#include<functional>
#include"QStyle"
#include<QRegularExpression>
#include<QNetworkReply>
#include<QByteArray>
#include<QJsonObject>    // 构造请求体/解析回包用的 JSON 对象
#include<QDir>           // 拼路径、转换路径分隔符
#include<QSettings>      // 读写 config.ini 这类配置文件
// 全局函数指针：让某个控件重新套用一次样式表。
// 为什么要手动刷？Qt 对 setProperty() 设置的自定义属性不敏感，
// 改了属性后 QSS 里 [state="xx"] 这类选择器不会自动生效，
// 必须 unpolish(取消)+polish(重新应用) 一次，控件才会按新属性重绘。
extern std::function<void(QWidget*)>repolish;
extern std::function<QString(QString)>xorString;
// 请求 ID：标识"这是哪一次操作"，服务器返回时靠它找到对应的处理逻辑
enum ReqId{
    ID_GET_VARIFY_CODE = 1001, //获取验证码
    ID_REG_USER = 1002, //注册用户
    ID_RESET_PWD = 1003, //重置密码
    ID_LOGIN_USER = 1004, //用户登录
    ID_CHAT_LOGIN = 1005, //登陆聊天服务器
    ID_CHAT_LOGIN_RSP= 1006, //登陆聊天服务器回包
};

// 统一错误码：网络层和业务层共用一套编号，便于判断成功与否
enum ErrorCodes{//错误原因
    SUCCESS = 0,
    ERR_JSON = 1, //Json解析失败
    ERR_NETWORK = 2,//网络错误
};

// 模块编号：标识请求是"哪个界面"发起的，HttpMgr 靠它决定把结果转发给谁
enum Modules{
    REGISTERMOD = 0,//注册模块
    RESETMOD = 1,
    LOGINMOD = 2,
};

enum TipErr {
    TIP_SUCCESS = 0,        // 操作成功
    TIP_EMAIL_ERR = 1,      // 邮箱格式错误
    TIP_PWD_ERR = 2,        // 密码格式错误
    TIP_CONFIRM_ERR = 3,    // 确认密码为空
    TIP_PWD_CONFIRM = 4,    // 两次密码不一致
    TIP_VARIFY_ERR = 5,     // 验证码错误
    TIP_USER_ERR = 6        // 用户名错误
};
enum ClickLbState{
    Normal = 0,//睁眼
    Selected = 1
};
// 后端服务器地址前缀，形如 "http://localhost:8080"（不含具体接口名）。
// 由 main.cpp 启动时从 config.ini 里读出来赋值；各界面拿它拼上接口名就能发请求，
// 这样换服务器只需改配置文件，不用重新编译。
extern QString gate_url_prefix;

#endif // GLOBAL_H
