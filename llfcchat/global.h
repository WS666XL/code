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
    ID_SEARCH_USER_REQ = 1007, //用户搜索请求
    ID_SEARCH_USER_RSP = 1008, //搜索用户回包
    ID_ADD_FRIEND_REQ = 1009,  //添加好友申请
    ID_ADD_FRIEND_RSP = 1010, //申请添加好友回复
    ID_NOTIFY_ADD_FRIEND_REQ = 1011,  //通知用户添加好友申请
    ID_AUTH_FRIEND_REQ = 1013,  //认证好友请求
    ID_AUTH_FRIEND_RSP = 1014,  //认证好友回复
    ID_NOTIFY_AUTH_FRIEND_REQ = 1015, //通知用户认证好友申请
    ID_TEXT_CHAT_MSG_REQ  = 1017,  //文本聊天信息请求
    ID_TEXT_CHAT_MSG_RSP  = 1018,  //文本聊天信息回复
    ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019, //通知用户文本聊天信息
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
    RESETMOD = 1,//重置密码
    LOGINMOD = 2,//登陆模块
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
enum ChatUIMode{
    SearchMode,//搜索模式
    ChatMode,//聊天模式
    ContactMode,//联系模式
};
//自定义QListWidgetItem的几种类型
enum ListItemType{
    CHAT_USER_ITEM,    //聊天用户
    CONTACT_USER_ITEM, //联系人用户
    SEARCH_USER_ITEM,  //搜索到的用户
    ADD_USER_TIP_ITEM, //提示添加用户
    INVALID_ITEM,      //不可点击条目
    GROUP_TIP_ITEM,    //分组提示条目
    LINE_ITEM,  //分割线
    APPLY_FRIEND_ITEM, //好友申请
};
enum class ChatRole
{
    Self,
    Other
};
struct MsgInfo{
    QString msgFlag;//"text,image,file"
    QString content;//表示文件和图像的url,文本信息
    QPixmap pixmap;//文件和图片的缩略图
};
//申请好友标签输入框最低长度
const int MIN_APPLY_LABEL_ED_LEN = 40;

const QString add_prefix = "添加标签 ";

const int  tip_offset = 5;//标签间距/边距
struct ServerInfo{
    QString Host;
    QString Port;
    QString Token;
    int Uid;
};
// 后端服务器地址前缀，形如 "http://localhost:8080"（不含具体接口名）。
// 由 main.cpp 启动时从 config.ini 里读出来赋值；各界面拿它拼上接口名就能发请求，
// 这样换服务器只需改配置文件，不用重新编译。
extern QString gate_url_prefix;
inline std::vector<QString>  strs ={"hello world !",
                             "nice to meet u",
                             "New year，new life",
                             "You have to love yourself",
                             "My love is written in the wind ever since the whole world is you"};

inline std::vector<QString> heads = {
    ":/res/head_1.jpg",
    ":/res/head_2.jpg",
    ":/res/head_3.jpg",
    ":/res/head_4.jpg",
    ":/res/head_5.jpg"
};

inline std::vector<QString> names = {
    "人机",
    "zack",
    "golang",
    "cpp",
    "java",
    "nodejs",
    "python",
    "rust"
};
#endif // GLOBAL_H
