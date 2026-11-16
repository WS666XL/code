#include "usermgr.h"
#include "qjsonarray.h"
UserMgr::~UserMgr()
{

}

void UserMgr::SetName(QString name)
{
    _user_info->_name = name;
}

void UserMgr::SetUid(int uid)
{
    _user_info->_uid= uid;
}

void UserMgr::SetToken(QString token)
{
    _token = token;
}

QString UserMgr::GetToken()
{
    return _token;
}

int UserMgr::GetUid()
{
    return _user_info->_uid;
}

std::shared_ptr<UserInfo> UserMgr::GetUserInfo()
{
    return _user_info;
}

QString UserMgr::GetName()
{
    return _user_info->_name;
}

std::vector<std::shared_ptr<ApplyInfo> > UserMgr::GetApplyList()
{
    return _apply_list;
}

bool UserMgr::AlreadyApply(int uid)
{
    for(auto& apply:_apply_list)
    {
        if(apply->_uid==uid)
        {
            return true;
        }
    }
    return false;
}

void UserMgr::AddApplyList(std::shared_ptr<ApplyInfo> app)
{
    _apply_list.push_back(app);
}

void UserMgr::SetUserInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info=user_info;
}

void UserMgr::AppendApplyList(QJsonArray array)
{
    // 遍历 QJsonArray 并输出每个元素
    for (const QJsonValue &value : array) {
        auto name = value["name"].toString();
        auto desc = value["desc"].toString();
        auto icon = value["icon"].toString();
        auto nick = value["nick"].toString();
        auto sex = value["sex"].toInt();
        auto uid = value["uid"].toInt();
        auto status = value["status"].toInt();
        auto info = std::make_shared<ApplyInfo>(uid, name,
                                                desc, icon, nick, sex, status);
        _apply_list.push_back(info);
    }
}

void UserMgr::AppendFriendList(QJsonArray array)
{
    // 遍历 QJsonArray 并输出每个元素
    for (const QJsonValue& value : array) {
        auto name = value["name"].toString();
        auto desc = value["desc"].toString();
        auto icon = value["icon"].toString();
        auto nick = value["nick"].toString();
        auto sex = value["sex"].toInt();
        auto uid = value["uid"].toInt();

        //好友统一用 UserInfo 存（旧的 FriendInfo 已并入 UserInfo），备注字段本版暂不展示
        auto info = std::make_shared<UserInfo>(uid, name,
                                                 nick, icon, sex, "", desc);
        _friend_list.push_back(info);
        _friend_map.insert(uid, info);
    }
}

bool UserMgr::CheckFriendById(int uid)
{
    auto iter = _friend_map.find(uid);
    if(iter == _friend_map.end()){
        return false;
    }

    return true;
}

void UserMgr::AddFriend(std::shared_ptr<AuthRsp> auth_rsp)
{
    auto friend_info = std::make_shared<UserInfo>(auth_rsp);
    _friend_map[friend_info->_uid] = friend_info;
}

void UserMgr::AddFriend(std::shared_ptr<AuthInfo> auth_info)
{
    auto friend_info = std::make_shared<UserInfo>(auth_info);
    _friend_map[friend_info->_uid] = friend_info;
}

std::shared_ptr<UserInfo> UserMgr::GetFriendById(int uid)
{
    auto find_it = _friend_map.find(uid);
    if(find_it == _friend_map.end()){
        return nullptr;
    }

    return *find_it;
}

void UserMgr::UpdateUserIcon(int uid, const QString& icon)
{
    if (icon.isEmpty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(_mtx);
    //自己：更新登录用户信息（当前会话里自己换头像走本地刷新，这里兜底）
    if (_user_info && _user_info->_uid == uid) {
        _user_info->_icon = icon;
        return;
    }
    //好友：_friend_list 与 _friend_map 存的是同一批 shared_ptr，
    //改 map 里的对象，列表侧读到的也是新值
    auto find_it = _friend_map.find(uid);
    if (find_it != _friend_map.end()) {
        (*find_it)->_icon = icon;
    }
}

std::vector<std::shared_ptr<UserInfo> > UserMgr::GetChatListPerPage()
{
    std::vector<std::shared_ptr<UserInfo>> friend_list;
    int begin = _chat_loaded;
    int end = begin + CHAT_COUNT_PER_PAGE;

    if (begin >= _friend_list.size()) {
        return friend_list;
    }

    if (end > _friend_list.size()) {
        friend_list = std::vector<std::shared_ptr<UserInfo>>(_friend_list.begin() + begin, _friend_list.end());
        return friend_list;
    }


    friend_list = std::vector<std::shared_ptr<UserInfo>>(_friend_list.begin() + begin, _friend_list.begin()+ end);
    return friend_list;
}

bool UserMgr::IsLoadChatFin()
{
    if (_chat_loaded >= _friend_list.size()) {
        return true;
    }

    return false;
}

void UserMgr::UpdateChatLoadedCount()
{
    int begin = _chat_loaded;
    int end = begin + CHAT_COUNT_PER_PAGE;

    if (begin >= _friend_list.size()) {
        return ;
    }

    if (end > _friend_list.size()) {
        _chat_loaded = _friend_list.size();
        return ;
    }

    _chat_loaded = end;
}

std::vector<std::shared_ptr<UserInfo> > UserMgr::GetConListPerPage()
{
    std::vector<std::shared_ptr<UserInfo>> friend_list;
    int begin = _contact_loaded;
    int end = begin + CHAT_COUNT_PER_PAGE;

    if (begin >= _friend_list.size()) {
        return friend_list;
    }

    if (end > _friend_list.size()) {
        friend_list = std::vector<std::shared_ptr<UserInfo>>(_friend_list.begin() + begin, _friend_list.end());
        return friend_list;
    }


    friend_list = std::vector<std::shared_ptr<UserInfo>>(_friend_list.begin() + begin, _friend_list.begin() + end);
    return friend_list;
}

void UserMgr::UpdateContactLoadedCount()
{
    int begin = _contact_loaded;
    int end = begin + CHAT_COUNT_PER_PAGE;

    if (begin >= _friend_list.size()) {
        return;
    }

    if (end > _friend_list.size()) {
        _contact_loaded = _friend_list.size();
        return;
    }

    _contact_loaded = end;
}

bool UserMgr::IsLoadConFin()
{
    if (_contact_loaded >= _friend_list.size()) {
        return true;
    }

    return false;
}

//==== 聊天会话缓存 ====

int UserMgr::GetLastChatThreadId()
{
    std::lock_guard<std::mutex> lock(_mtx);
    return _last_chat_thread_id;
}

void UserMgr::SetLastChatThreadId(int id)
{
    std::lock_guard<std::mutex> lock(_mtx);
    _last_chat_thread_id = id;
}

void UserMgr::AddChatThreadData(std::shared_ptr<ChatThreadData> chat_thread_data, int other_uid)
{
    std::lock_guard<std::mutex> lock(_mtx);
    //建立会话id到数据的映射关系
    _chat_map[chat_thread_data->GetThreadId()] = chat_thread_data;
    //存储会话列表（保持 1026 回包顺序，预加载按这个顺序推进）
    _chat_thread_ids.push_back(chat_thread_data->GetThreadId());
    if (other_uid) {
        //将对方uid和会话id关联，跳转聊天时从 uid 一把查到会话
        _uid_to_thread_id[other_uid] = chat_thread_data->GetThreadId();
    }
}

int UserMgr::GetThreadIdByUid(int uid)
{
   std::lock_guard<std::mutex> lock(_mtx);
   auto iter = _uid_to_thread_id.find(uid);
   if (iter == _uid_to_thread_id.end()){
       return -1;
   }

   return iter.value();
}

std::shared_ptr<ChatThreadData> UserMgr::GetChatThreadByThreadId(int thread_id)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto find_iter = _chat_map.find(thread_id);
    if (find_iter != _chat_map.end()) {
        return find_iter.value();
    }
    return nullptr;
}

std::shared_ptr<ChatThreadData> UserMgr::GetChatThreadByUid(int uid) {
    std::lock_guard<std::mutex> lock(_mtx);
    //先查 uid → thread_id，再查 thread_id → 会话数据
    auto iter = _uid_to_thread_id.find(uid);
    if (iter == _uid_to_thread_id.end()) {
        return nullptr;
    }

    auto chat_iter = _chat_map.find(iter.value());
    if(chat_iter == _chat_map.end()) {
        return nullptr;
    }

    return chat_iter.value();
}

//取当前要预加载历史消息的会话（登录后从第一个会话开始）
std::shared_ptr<ChatThreadData> UserMgr::GetCurLoadData()
{
    std::lock_guard<std::mutex> lock(_mtx);
    if (_cur_load_chat_index >= _chat_thread_ids.size()) {
        return nullptr;
    }

    auto iter = _chat_map.find(_chat_thread_ids[_cur_load_chat_index]);
    if (iter == _chat_map.end()) {
        return nullptr;
    }

    return iter.value();
}

//==== 图片传输管理：登记/查询/移除传输中的图片消息 ====
//发送方流程：选图后登记 → 1035 发出 → 1036 回包按名字取回发起 1037 → 传完移除
//接收方流程：1039 通知时登记（占位）→ 1034 下载完成按名字取回刷新气泡 → 移除
void UserMgr::AddTransFile(QString name, std::shared_ptr<MsgInfo> msg_info)
{
    std::lock_guard<std::mutex> lock(_mtx);
    _trans_files.insert(name, msg_info);
}

std::shared_ptr<MsgInfo> UserMgr::GetTransFileByName(QString name)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto iter = _trans_files.find(name);
    if (iter == _trans_files.end()) {
        return nullptr;
    }
    return iter.value();
}

void UserMgr::RmvTransFile(QString name)
{
    std::lock_guard<std::mutex> lock(_mtx);
    _trans_files.remove(name);
}

//预加载推进到下一个会话；越界返回 nullptr 表示全部加载完
std::shared_ptr<ChatThreadData> UserMgr::GetNextLoadData() {
    std::lock_guard<std::mutex> lock(_mtx);
    _cur_load_chat_index++;
    if (_cur_load_chat_index >= _chat_thread_ids.size()) {
        return nullptr;
    }

    auto iter = _chat_map.find(_chat_thread_ids[_cur_load_chat_index]);
    if (iter == _chat_map.end()) {
        return nullptr;
    }

    return iter.value();
}

UserMgr::UserMgr():_user_info(nullptr), _chat_loaded(0),_contact_loaded(0),
    _cur_load_chat_index(0),_last_chat_thread_id(0)
{

}
