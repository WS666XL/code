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
#include<QSet>           // 上传拥塞窗口：记录已发未回执/已回执的分块序号
// 全局函数指针：让某个控件重新套用一次样式表。
// 为什么要手动刷？Qt 对 setProperty() 设置的自定义属性不敏感，
// 改了属性后 QSS 里 [state="xx"] 这类选择器不会自动生效，
// 必须 unpolish(取消)+polish(重新应用) 一次，控件才会按新属性重绘。
extern std::function<void(QWidget*)>repolish;
extern std::function<QString(QString)>xorString;
// 头像统一加载入口（原图解码+缩放双层缓存，见 global.cpp）。
// icon 支持三种形态，按优先级解析：
//   1. ":/res/head_X.jpg" —— 随包默认头像资源路径
//   2. 纯数字（如 "1"）   —— 老数据默认头像编号，转成形态 1
//   3. 服务器文件名（如 "head.png"）—— 从 AppData/<ownerUid>/avatars/ 读本地文件，
//      本地没有时自动发 1033 下载（带 ownerUid 防同名冲突），下载完成自动刷新 label
// ownerUid 是头像所有者的 uid；不传（0）时默认用当前登录 uid（自己的头像）
QPixmap LoadAvatarPixmap(const QString& icon, const QSize& size, int owner_uid = 0);
// 自己上传新头像后调用：清自己 uid 的内存缓存并重刷所有显示自己头像的
// label（本地文件已被 slot_up_load 重写为新图，重载立即生效，无需下载）
void RefreshSelfAvatar();
// 带 label 的重载：本地缺文件触发下载后，下载完成会自动把新头像 set 回该 label
//（label 用 QPointer 弱引用跟踪，item 销毁后自动失效，不会悬空访问）
class QLabel;   // 前向声明：引用参数不需要完整定义，避免在所有翻译单元里拖入 QLabel 头
QPixmap LoadAvatarPixmap(QLabel* label, const QString& icon, const QSize& size, int owner_uid = 0);
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
    ID_NOTIFY_OFF_LINE_REQ = 1021, //通知用户下线（同账号异地登录被踢）
    ID_HEART_BEAT_REQ = 1023,      //心跳请求
    ID_HEARTBEAT_RSP = 1024,       //心跳回复
    ID_LOAD_CHAT_THREAD_REQ = 1025,  //加载聊天线程列表请求（登录后拉取会话列表，按 thread_id 翻页）
    ID_LOAD_CHAT_THREAD_RSP = 1026,  //加载聊天线程列表回复
    ID_CREATE_PRIVATE_CHAT_REQ = 1027, //创建私聊请求（没有现成会话时先建线程再聊天）
    ID_CREATE_PRIVATE_CHAT_RSP = 1028, //创建私聊回复（带回 thread_id）
    ID_LOAD_CHAT_MSG_REQ = 1029,     //分页加载某个会话的历史消息请求
    ID_LOAD_CHAT_MSG_RSP = 1030,     //分页加载历史消息回复
    ID_IMG_CHAT_MSG_REQ = 1035,      //图片聊天消息请求（先入库拿 message_id，文件随后走 1037 传资源服务器）
    ID_IMG_CHAT_MSG_RSP = 1036,      //图片聊天消息回复（带入库后的 message_id，收到后才开始传文件）
    ID_NOTIFY_IMG_CHAT_MSG_REQ = 1039, //通知用户图片聊天信息（资源服务器落盘完成后经 ChatServer 转发）
    ID_CREATE_GROUP_REQ = 1055,      //建群请求（发起人选好友成群；老师体系未用 1050+ 段）
    ID_CREATE_GROUP_RSP = 1056,      //建群回复（带回 thread_id/群名/成员列表）
    ID_NOTIFY_AVATAR_UPDATE_REQ = 1057, //头像变更通知（服务端推在线用户，客户端删缓存重新下载回显）
    ID_DELETE_CHAT_MSG_REQ = 1061,   //删除单条消息（只删自己视角，落 chat_message_deleted）
    ID_DELETE_CHAT_MSG_RSP = 1062,   //删除单条消息回包
    ID_SET_FRIEND_REMARK_REQ = 1063, //设置好友备注（写 friend.back）
    ID_SET_FRIEND_REMARK_RSP = 1064, //设置好友备注回包
    ID_DELETE_FRIEND_REQ = 1065,     //删除好友（只删自己这一侧）
    ID_DELETE_FRIEND_RSP = 1066,     //删除好友回包
    ID_UPDATE_USER_INFO_REQ = 1020,  //修改个人资料（昵称/描述）
    ID_UPDATE_USER_INFO_RSP = 1022,  //修改个人资料回包
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
    SettingsMode,//设置模式（day36：侧边栏"设置"进入用户信息页）
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

//消息状态：自己发的消息气泡旁边的小图标（未送达/送达/已读）用
enum MsgStatus{
    UN_READ = 0,      //对方未读（刚发出去还没收到回执）
    SEND_FAILED = 1,  //发送失败
    READED = 2,       //对方已读（服务端落库后回执默认给 2）
};

//聊天形式：本轮只做私聊，群聊留待后续
enum class ChatFormType {
    PRIVATE = 0,  //私聊
    GROUP = 1     //群聊
};

//聊天消息类型：1030 回包里的 msg_type 按这个枚举解释；本轮只处理文本
enum class ChatMsgType {
    TEXT = 0,  //文本
    PIC = 1,   //图片（本轮跳过）
    VIDEO = 2, //视频
    FILE = 3   //文件
};

//传输方向：气泡上的"暂停/继续"要知道该操作上传还是下载
enum class TransferType {
    None,
    Upload,
    Download
};

//传输状态：与老师版 day40 一致，进度条文字/暂停按钮都按它切换
enum class TransferState {
    None,          //空闲（历史消息/已完成后再无动作）
    Uploading,     //上传中
    Downloading,   //下载中
    Paused,        //已暂停（点继续后走 1005 同步进度 / 1033 续块）
    Completed,     //完成
    Failed         //失败（可重试）
};
struct MsgInfo{
    QString msgFlag;//"text,image,file"
    QString content;//图片消息=本地文件路径（上传用），文本消息=文字内容
    QPixmap pixmap;//文件和图片的缩略图

    // ===== 图片/文件消息专用字段（day39 图片聊天 / day41 文件聊天）=====
    QString _md5;           //文件 MD5（秒传判断 + 断点续传进度 key，服务端 key=uid|md5）
    QString _unique_name;   //上传后的唯一文件名（uuid+原后缀），落盘/下载/历史记录都用它
    QString _file_name;     //原始文件名（文件卡片显示用；图片消息为空）
    qint64 _total_size = 0; //文件字节数（1035 请求与占位气泡用）
    int _msg_id = 0;        //服务端 chat_message.message_id（1036 回包回填）
    int _sender = 0;        //发送者 uid
    int _receiver = 0;      //接收者 uid

    // ===== 上传拥塞窗口状态（day40：1037 每块回执驱动滑窗续发）=====
    qint64 _seq = 0;                  // 已发出的最大分块序号（0 表示还没发）
    qint64 _max_seq = 0;              // 总分块数 = ceil(total_size / MAX_FILE_LEN)
    qint64 _rsp_size = 0;             // 服务端已确认收到的字节数（进度条用）
    QSet<int> _flighting_seqs;        // 已发但还没回 1038 的分块
    QSet<int> _rsp_seqs;              // 已收到 1038 的分块
    int _last_confirmed_seq = 0;      // 从 1 开始连续确认到的最大序号

    // ===== day38 断点续传状态 =====
    TransferState _transfer_state = TransferState::None;  //上传/下载中、暂停等
    TransferType _transfer_type = TransferType::None;     //这份资源当前在传哪个方向
};
//申请好友标签输入框最低长度
const int MIN_APPLY_LABEL_ED_LEN = 40;

const QString add_prefix = "添加标签 ";

const int  tip_offset = 5;//标签间距/边距
struct ServerInfo{
    QString Host;      //聊天服务器 host（GateServer 登录回包下发）
    QString Port;      //聊天服务器 port
    QString Token;     //登录令牌（连 ChatServer 后 ID_CHAT_LOGIN 带它验证）
    int Uid;
    QString ResHost;   //资源服务器 host（day39：GateServer 从配置读出随登录回包下发）
    QString ResPort;   //资源服务器 port（一般 9090）
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
    ":/res/head_5.jpg",
    ":/res/head_6.jpg",
    ":/res/head_7.jpg",
    ":/res/head_8.jpg"
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
const int CHAT_COUNT_PER_PAGE=12;

// ===== day31 文件传输相关宏（资源服务器专用，独立于上面的 ReqId 体系）=====
//TCP文件上传包头长度（2B消息id + 4B包体长度）
#define FILE_UPLOAD_HEAD_LEN 6
#define FILE_UPLOAD_ID_LEN 2
#define FILE_UPLOAD_LEN_LEN 4
//最大文件长度（每块 32KB，base64 编码后约 44KB，资源服务器接受）
#define MAX_FILE_LEN (1024*32)
//上传拥塞窗口：同一时刻最多允许 MAX_CWND_SIZE 个分块"已发未回执"。
//为什么不一口气发完：整文件一次性塞进 socket 会占满发送缓冲，任何一块丢失
//都只能整文件重传；滑窗把在途数据限制在窗口内，靠 1038 回执逐块放行，
//暂停/继续时再通过 1005 向服务器同步"已确认到第几块"，从断点续传
#define MAX_CWND_SIZE 5
#endif // GLOBAL_H
