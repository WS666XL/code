#ifndef USERDATA_H
#define USERDATA_H
#include <QString>
#include <memory>
#include <QJsonArray>
#include <vector>
#include <QJsonObject>
#include <QMap>
#include "global.h"

//==================== 用户/申请类数据结构 ====================

class SearchInfo {
public:
    SearchInfo(int uid, QString name, QString nick, QString desc, int sex, QString icon);
    SearchInfo() = default;
    int _uid;
    QString _name;
    QString _nick;
    QString _desc;
    int _sex;
    QString _icon;
};

Q_DECLARE_METATYPE(SearchInfo)
Q_DECLARE_METATYPE(std::shared_ptr<SearchInfo>)

class AddFriendApply {
public:
    AddFriendApply(int from_uid, QString name, QString desc,
                   QString icon, QString nick, int sex);
    AddFriendApply() = default;
    int _from_uid;
    QString _name;
    QString _desc;
    QString _icon;
    QString _nick;
    int     _sex;
};

Q_DECLARE_METATYPE(std::shared_ptr<AddFriendApply>)

struct ApplyInfo {
    ApplyInfo() = default;
    ApplyInfo(int uid, QString name, QString desc,
        QString icon, QString nick, int sex, int status)
        :_uid(uid),_name(name),_desc(desc),
        _icon(icon),_nick(nick),_sex(sex),_status(status){}

    ApplyInfo(std::shared_ptr<AddFriendApply> addinfo)
        :_uid(addinfo->_from_uid),_name(addinfo->_name),
          _desc(addinfo->_desc),_icon(addinfo->_icon),
          _nick(addinfo->_nick),_sex(addinfo->_sex),
          _status(0)
    {}
    void SetIcon(QString head){
        _icon = head;
    }
    int _uid;
    QString _name;
    QString _desc;
    QString _icon;
    QString _nick;
    int _sex;
    int _status;
};

//为什么前向声明：AuthInfo/AuthRsp 里要存聊天数据指针，但完整定义在下面
class TextChatData;

//认证好友（我同意别人）时对方带过来的信息。
//服务端认证回包可能附带历史聊天消息和会话 id，用来在聊天列表里直接建条目
struct AuthInfo {
    AuthInfo(int uid, QString name,
             QString nick, QString icon, int sex):
        _uid(uid), _name(name), _nick(nick), _icon(icon),
        _sex(sex), _thread_id(0){}
    AuthInfo() = default;
    void SetChatDatas(std::vector<std::shared_ptr<TextChatData>> _chat_datas);
    int _uid;
    QString _name;
    QString _nick;
    QString _icon;
    int _sex;
    int _thread_id;   //会话 id，没有则为 0
    std::vector<std::shared_ptr<TextChatData>> _chat_datas;
};

Q_DECLARE_METATYPE(std::shared_ptr<AuthInfo>)

//我申请认证好友，对方同意后收到的回包信息（字段含义同 AuthInfo）
struct AuthRsp {
    AuthRsp() = default;
    AuthRsp(int peer_uid, QString peer_name,
            QString peer_nick, QString peer_icon, int peer_sex)
        :_uid(peer_uid),_name(peer_name),_nick(peer_nick),
          _icon(peer_icon),_sex(peer_sex),_thread_id(0)
    {

    }

    void SetChatDatas(std::vector<std::shared_ptr<TextChatData>> _chat_datas);
    int _uid;
    QString _name;
    QString _nick;
    QString _icon;
    int _sex;
    int _thread_id;
    std::vector<std::shared_ptr<TextChatData>> _chat_datas;
};

Q_DECLARE_METATYPE(std::shared_ptr<AuthRsp>)

//通用的"一个人"的信息：聊天列表、联系人列表、资料页都用它
//（旧的 FriendInfo/UserInfo 两套结构合并成这一套，消息都挪到 ChatThreadData 里管理）
struct UserInfo {
    UserInfo(int uid, QString name, QString nick, QString icon, int sex, QString last_msg = "", QString desc=""):
        _uid(uid),_name(name),_nick(nick),_icon(icon),_sex(sex),_desc(desc){ Q_UNUSED(last_msg); }

    UserInfo(std::shared_ptr<AuthInfo> auth):
        _uid(auth->_uid),_name(auth->_name),_nick(auth->_nick),
        _icon(auth->_icon),_sex(auth->_sex),_desc(""){}

    UserInfo(int uid, QString name, QString icon):
    _uid(uid), _name(name), _nick(_name),_icon(icon),
    _sex(0),_desc(""){

    }

    UserInfo(std::shared_ptr<AuthRsp> auth):
        _uid(auth->_uid),_name(auth->_name),_nick(auth->_nick),
        _icon(auth->_icon),_sex(auth->_sex),_desc(""){}

    UserInfo(std::shared_ptr<SearchInfo> search_info):
        _uid(search_info->_uid),_name(search_info->_name),_nick(search_info->_nick),
    _icon(search_info->_icon),_sex(search_info->_sex), _desc(search_info->_desc){

    }

    UserInfo() = default;

    int _uid;
    QString _name;
    QString _nick;
    QString _icon;
    int _sex;
    QString _desc;
};

Q_DECLARE_METATYPE(std::shared_ptr<UserInfo>)

//==================== 聊天消息类数据结构 ====================

//聊天消息的抽象基类：文本/图片/文件消息都继承它，
//聊天记录容器（ChatThreadData）统一按基类指针存取，
//这样新增消息类型时不用改动存储层的结构
class ChatDataBase {
public:
    ChatDataBase() = default;
    //从服务端历史记录构造：带服务端消息 id
    ChatDataBase(int msg_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type,
        QString content, int send_uid, int status, QString chat_time);
    //自己刚发出去还没拿到服务端 id 的消息：只有本地唯一 id
    ChatDataBase(QString unique_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type,
        QString content, int send_uid, int status, QString chat_time);
    //收到服务端回执后：本地唯一 id + 服务端消息 id 都有
    ChatDataBase(int msg_id, QString unique_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type,
        QString content, int send_uid, int status, QString chat_time);
    int GetMsgId() { return _msg_id; }
    int GetThreadId() { return _thread_id; }
    ChatFormType GetFormType() { return _form_type; }
    ChatMsgType GetMsgType() { return _msg_type; }
    QString GetContent() { return _content; }
    int GetSendUid() { return _send_uid; }
    QString GetMsgContent(){return _content;}
    void SetUniqueId(QString unique_id) { _unique_id = unique_id; }
    QString GetUniqueId() { return _unique_id; }
    int GetStatus() { return _status; }
    QString GetChatTime() { return _chat_time; }
    void SetMsgId(int msg_id) { _msg_id = msg_id; }
    void SetStatus(int status) { _status = status; }
    virtual ~ChatDataBase() {}  //虚析构：基类指针 delete 派生类对象时才能正确释放
protected:
    QString _unique_id;  //客户端本地唯一标识（发消息时用 QUuid 生成，服务端回执靠它匹配）
    int _msg_id;         //服务端消息 id（落库后才有）
    int _thread_id;      //会话 id
    ChatFormType _form_type;  //群聊还是私聊
    ChatMsgType _msg_type;    //文本为0，图片为1，文件为2
    QString _content;         //消息内容
    int _send_uid;            //发送者 id
    int _status;              //消息状态（MsgStatus）
    QString _chat_time;       //聊天时间
};

Q_DECLARE_METATYPE(std::vector<std::shared_ptr<ChatDataBase>>)

//文本消息：本轮只实现这一种，逻辑都在基类里，这里只做类型标注
class TextChatData : public ChatDataBase {
public:

    TextChatData(int msg_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type, QString content,
        int send_uid, int status, QString chat_time="") :
        ChatDataBase(msg_id, thread_id, form_type, msg_type, content, send_uid, status, chat_time)
    {

    }

    TextChatData(QString unique_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type, QString content,
        int send_uid, int status, QString chat_time="") :
        ChatDataBase(unique_id, thread_id, form_type, msg_type, content, send_uid, status, chat_time)
    {

    }

    TextChatData(int msg_id, QString unique_id, int thread_id, ChatFormType form_type, ChatMsgType msg_type, QString content,
        int send_uid, int status, QString chat_time = "") :
        ChatDataBase(msg_id, unique_id, thread_id, form_type, msg_type, content, send_uid, status, chat_time)
    {

    }

    TextChatData() = default;

    ~TextChatData() override{}
};

Q_DECLARE_METATYPE(std::vector<std::shared_ptr<TextChatData>>)

//图片消息数据：继承 ChatDataBase 的公共字段（msg_id/thread_id/发送者/状态），
//额外挂一份 MsgInfo（图片本地路径、唯一文件名、缩略图等）。
//发送者用它组织 1035/1037，接收者用它建占位气泡并触发图片下载
class ImgChatData : public ChatDataBase {
public:
    ImgChatData(std::shared_ptr<MsgInfo> msg_info, QString unique_id,
        int thread_id, ChatFormType form_type, ChatMsgType msg_type,
        int send_uid, int status, QString chat_time = "")
        : ChatDataBase(unique_id, thread_id, form_type, msg_type,
            msg_info->_unique_name, send_uid, status, chat_time),
        _msg_info(msg_info)
    {
        //图片消息的 msg_id 存在 MsgInfo 里（1036 回包后回填），同步到基类字段
        _msg_id = _msg_info->_msg_id;
    }

    ~ImgChatData() override {}

    std::shared_ptr<MsgInfo> _msg_info;
};

Q_DECLARE_METATYPE(std::shared_ptr<ImgChatData>)

//一条聊天线程（会话）的描述信息，来自 1026 回包的 threads 数组
struct ChatThreadInfo {
    int _thread_id;
    QString _type;     // "private" 私聊 or "group" 群聊
    int _user1_id;     //私聊时对应 private_chat.user1_id；群聊时设为 0
    int _user2_id;     //私聊时对应 private_chat.user2_id；群聊时设为 0
    QString _group_name; //群聊时的群名（服务端 1026 回包 join group_chat 带出；私聊为空）
    ChatThreadInfo() = default;
};

Q_DECLARE_METATYPE(std::vector<std::shared_ptr<ChatThreadInfo>>)

//把服务端时间串("yyyy-MM-dd HH:mm:ss")格式化成微信风格的列表时间：
//今天→"HH:mm"，昨天→"昨天"，一周内→"星期X"，今年→"MM/dd"，往年→"yyyy/MM/dd"
QString FormatChatListTime(const QString& chat_time);

//客户端本地的一个会话：缓存该会话的所有消息 + 未收到回执的消息 + 最后一条消息
//聊天列表条目(ChatUserWid)和聊天页面(ChatPage)都从这里取数据
class ChatThreadData {
public:
    ChatThreadData() = default;
    //form_type 缺省私聊：老的构造调用点不用改；群会话传 ChatFormType::GROUP
    ChatThreadData(int other_id, int thread_id, int last_msg_id,
        ChatFormType form_type = ChatFormType::PRIVATE)
        :_other_id(other_id), _thread_id(thread_id), _last_msg_id(last_msg_id),
         _form_type(form_type){}
    void AddMsg(std::shared_ptr<ChatDataBase> msg);   //按服务端 id 归档一条消息
    void MoveMsg(std::shared_ptr<ChatDataBase> msg);  //收到回执：把"未回复"的消息挪进正式缓存
    void SetLastMsgId(int msg_id);
    void SetOtherId(int other_id);
    int  GetOtherId();
    QMap<int, std::shared_ptr<ChatDataBase>> GetMsgMap();
    int  GetThreadId();
    QMap<int, std::shared_ptr<ChatDataBase>>&  GetMsgMapRef();
    void AppendMsg(int msg_id, std::shared_ptr<ChatDataBase> base_msg); //装载历史记录用
    QString GetLastMsg();
    QString GetLastMsgTime();  //最后一条消息的时间（微信风格格式化后）
    int GetLastMsgId();
    QMap<QString, std::shared_ptr<ChatDataBase>>& GetMsgUnRspRef();
    void AppendUnRspMsg(QString unique_id, std::shared_ptr<ChatDataBase> base_msg);
    // ===== day41 群聊 =====
    bool IsGroup() const { return _form_type == ChatFormType::GROUP; }
    QString GetGroupName() const { return _group_name; }
    void SetGroupName(const QString& name) { _group_name = name; }
private:
    //如果是私聊，则为对方的id；如果是群聊，则为0
    int _other_id = 0;
    int _thread_id = 0;
    int _last_msg_id = 0;
    //会话形式：私聊/群聊（决定气泡 role 判定、列表条目显示、消息发送的目标字段）
    ChatFormType _form_type = ChatFormType::PRIVATE;
    QString _group_name;     //群聊时的群名（会话条目/聊天页标题显示用）
    QString _last_msg;       //最后一条消息的文本，聊天列表条目显示用
    QString _last_msg_time;  //最后一条消息的时间（服务端 "yyyy-MM-dd HH:mm:ss" 格式）
    //缓存消息 map，抽象为基类指针，兼容以后图片等其他类型消息
    //key 是服务端消息 id，保证按 id 顺序、去重
    QMap<int, std::shared_ptr<ChatDataBase>>  _msg_map;
    //已发送但还没收到服务端回执的消息，key 是本地生成的 unique_id
    //回执(1018)到了之后按 unique_id 找到它，补上 msg_id 挪进 _msg_map
    QMap<QString, std::shared_ptr<ChatDataBase>> _msg_unrsp_map;
};

#endif
