#ifndef USERMGR_H
#define USERMGR_H
#include <QObject>
#include <memory>
#include <singleton.h>
#include"userdata.h"
#include"global.h"
#include<QJsonArray>
/******************************************************************************
 *  文件：usermgr.h
 *  作用：全局保存"当前登录用户"的信息（用户名 / uid / token）
 *  设计：单例。登录成功后调SetXxx() 存进去，之后任何界面随时用
 *        UserMgr::GetInstance() 都能取到，不用一层层往下传参数
 *****************************************************************************/
class UserMgr:public QObject,public Singleton<UserMgr>,
        public std::enable_shared_from_this<UserMgr>
{
    Q_OBJECT   // QObject 才能用信号槽；虽然本类目前没写信号，但继承链里有
public:
    friend class Singleton<UserMgr>;   // 友元：只允许单例模板调用下面的私有构造函数，
                                       // 防止外部随手 new 出一个第二个实例
    ~ UserMgr();
    void SetName(QString name);       // 记录用户名（登录成功时写入）
    void SetUid(int uid);             // 记录用户 id
    void SetToken(QString token);     // 记录登录令牌，后续请求要带它去后端鉴权
    int GetUid();
    std::shared_ptr<UserInfo> GetUserInfo();
    QString GetName();
    std::vector<std::shared_ptr<ApplyInfo>> GetApplyList();
    bool AlreadyApply(int uid);
    void AddApplyList(std::shared_ptr<ApplyInfo>app);
    void SetUserInfo(std::shared_ptr<UserInfo>user_info);
    void AppendApplyList(QJsonArray array);
    void AppendFriendList(QJsonArray array);
     bool CheckFriendById(int uid);
    void AddFriend(std::shared_ptr<AuthRsp> auth_rsp);
    void AddFriend(std::shared_ptr<AuthInfo> auth_info);
    std::shared_ptr<FriendInfo> GetFriendById(int uid);

    std::vector<std::shared_ptr<FriendInfo>> GetChatListPerPage();
    bool IsLoadChatFin();
    void UpdateChatLoadedCount();
    std::vector<std::shared_ptr<FriendInfo>> GetConListPerPage();
    void UpdateContactLoadedCount();
    bool IsLoadConFin();
    void AppendFriendChatMsg(int friend_id,std::vector<std::shared_ptr<TextChatData>>msgs);
private:
    UserMgr();
    std::shared_ptr<UserInfo> _user_info;
    std::vector<std::shared_ptr<ApplyInfo>> _apply_list;
    std::vector<std::shared_ptr<FriendInfo>> _friend_list;
    QMap<int, std::shared_ptr<FriendInfo>> _friend_map;
    QString _token;
    int _chat_loaded;
    int _contact_loaded;
};

#endif // USERMGR_H
