#include "userdata.h"
#include <memory>
#include <QDateTime>

SearchInfo::SearchInfo(int uid, QString name,
                       QString nick, QString desc, int sex, QString icon):_uid(uid)
    ,_name(name), _nick(nick),_desc(desc),_sex(sex),_icon(icon){
}

AddFriendApply::AddFriendApply(int from_uid, QString name, QString desc,
                               QString icon, QString nick, int sex)
    :_from_uid(from_uid),_name(name),
    _desc(desc),_icon(icon),_nick(nick),_sex(sex)
{

}

//==== ChatDataBase：三个构造函数只是"哪些字段已知"的组合不同 ====

ChatDataBase::ChatDataBase(int msg_id, int thread_id, ChatFormType form_type,
    ChatMsgType msg_type, QString content, int send_uid, int status, QString chat_time)
    :_msg_id(msg_id),_thread_id(thread_id), _form_type(form_type),
    _msg_type(msg_type), _content(content), _send_uid(send_uid),
    _status(status), _chat_time(chat_time){

}

ChatDataBase::ChatDataBase(QString unique_id, int thread_id, ChatFormType form_type,
    ChatMsgType msg_type, QString content, int send_uid, int status, QString chat_time)
    :_unique_id(unique_id),_thread_id(thread_id), _form_type(form_type),
    _msg_type(msg_type), _content(content), _send_uid(send_uid),_msg_id(0),
    _status(status), _chat_time(chat_time)
{

}

ChatDataBase::ChatDataBase(int msg_id, QString unique_id, int thread_id, ChatFormType form_type,
    ChatMsgType msg_type, QString content, int send_uid, int status, QString chat_time)
    :_msg_id(msg_id), _unique_id(unique_id),_thread_id(thread_id), _form_type(form_type),
    _msg_type(msg_type), _content(content), _send_uid(send_uid),
    _status(status), _chat_time(chat_time) {

}

//==== ChatThreadData：一个会话的本地消息缓存 ====

//归档一条已确认（有服务端 msg_id）的消息，同时刷新"最后一条"显示
void ChatThreadData::AddMsg(std::shared_ptr<ChatDataBase> msg)
{
    _msg_map.insert(msg->GetMsgId(), msg);
    _last_msg = msg->GetMsgContent();
    _last_msg_time = msg->GetChatTime();
    _last_msg_id = msg->GetMsgId();
}

//收到 1018 回执：按 unique_id 在"未回复"表里找到之前发的消息，
//补上服务端返回的 msg_id/status，再挪进正式缓存。
//找不到（比如消息已重发过）就直接当新消息归档
void ChatThreadData::MoveMsg(std::shared_ptr<ChatDataBase> msg) {

    auto iter = _msg_unrsp_map.find(msg->GetUniqueId());
    if (iter == _msg_unrsp_map.end()) {
        AddMsg(msg);
        return;
    }

    //复用原来那个对象（页面气泡也持有它），只更新 id 和状态
    iter.value()->SetMsgId(msg->GetMsgId());
    iter.value()->SetStatus(msg->GetStatus());
    AddMsg(iter.value());
    _msg_unrsp_map.erase(iter);
}

void ChatThreadData::SetLastMsgId(int msg_id)
{
    _last_msg_id = msg_id;
}

void ChatThreadData::SetOtherId(int other_id)
{
    _other_id = other_id;
}

int ChatThreadData::GetOtherId() {
    return _other_id;
}

QMap<int, std::shared_ptr<ChatDataBase>> ChatThreadData::GetMsgMap() {
    return _msg_map;
}

int ChatThreadData::GetThreadId()
{
    return _thread_id;
}

QMap<int, std::shared_ptr<ChatDataBase>>& ChatThreadData::GetMsgMapRef()
{
    return _msg_map;
}

//装载历史消息（1029 回包逐条插入），也顺带刷新"最后一条"
void ChatThreadData::AppendMsg(int msg_id, std::shared_ptr<ChatDataBase> base_msg) {
    _msg_map.insert(msg_id, base_msg);
    _last_msg = base_msg->GetMsgContent();
    _last_msg_time = base_msg->GetChatTime();
    _last_msg_id = msg_id;
}

QString ChatThreadData::GetLastMsg()
{
    return _last_msg;
}

QString ChatThreadData::GetLastMsgTime()
{
    return FormatChatListTime(_last_msg_time);
}

//微信风格的聊天列表时间：按"距离现在多久"选择显示粒度
QString FormatChatListTime(const QString& chat_time)
{
    QDateTime msg_dt = QDateTime::fromString(chat_time, "yyyy-MM-dd HH:mm:ss");
    if (!msg_dt.isValid()) {
        //服务端时间串解析失败就原样显示，避免出现乱码
        return chat_time;
    }
    QDate msg_date = msg_dt.date();
    QDate today = QDate::currentDate();

    if (msg_date == today) {
        return msg_dt.toString("HH:mm");                 //今天：只显示时分
    }
    if (msg_date == today.addDays(-1)) {
        return QStringLiteral("昨天");                    //昨天
    }
    int days = msg_date.daysTo(today);
    if (days > 1 && days < 7) {
        //一周内：显示星期几
        static const QString week_names[] = {
            QStringLiteral("星期一"), QStringLiteral("星期二"), QStringLiteral("星期三"),
            QStringLiteral("星期四"), QStringLiteral("星期五"), QStringLiteral("星期六"),
            QStringLiteral("星期日")
        };
        return week_names[msg_date.dayOfWeek() - 1];
    }
    if (msg_date.year() == today.year()) {
        return msg_dt.toString("MM/dd");                 //今年更早：月/日
    }
    return msg_dt.toString("yyyy/MM/dd");                //往年：带年份
}

int ChatThreadData::GetLastMsgId()
{
    return _last_msg_id;
}

//自己发出、尚未收到回执的消息先挂在这里，等 1018 回来配对
void ChatThreadData::AppendUnRspMsg(QString unique_id, std::shared_ptr<ChatDataBase> base_msg)
{
    _msg_unrsp_map.insert(unique_id, base_msg);
}

QMap<QString, std::shared_ptr<ChatDataBase>>& ChatThreadData::GetMsgUnRspRef() {
    return _msg_unrsp_map;
}

//==== AuthInfo / AuthRsp：附带的历史聊天消息 ====

void AuthInfo::SetChatDatas(std::vector<std::shared_ptr<TextChatData>> chat_datas)
{
    //服务端可能不下发聊天数据（本版服务端就不下发），空数组直接跳过防止越界
    if (chat_datas.empty()) {
        return;
    }

    _chat_datas = chat_datas;
    _thread_id = _chat_datas[0]->GetThreadId();
}

void AuthRsp::SetChatDatas(std::vector<std::shared_ptr<TextChatData>> chat_datas) {
    if (chat_datas.empty()) {
        return;
    }

    _chat_datas = chat_datas;
    _thread_id = _chat_datas[0]->GetThreadId();
}
