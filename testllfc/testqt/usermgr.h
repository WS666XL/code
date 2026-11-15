#ifndef USERMGR_H
#define USERMGR_H
#include <QObject>
#include <memory>
#include <mutex>
#include <singleton.h>
#include"userdata.h"
#include"global.h"
#include<QJsonArray>
/******************************************************************************
 *  文件：usermgr.h
 *  作用：全局保存"当前登录用户"的信息（用户名 / uid / token），
 *        以及好友列表、申请列表、聊天会话（ChatThreadData）缓存
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
    int GetUid();                     // 自己的 uid（发请求时都要带上）
    QString GetToken();               // 取登录令牌
    QString GetName();
    std::shared_ptr<UserInfo> GetUserInfo();
    std::vector<std::shared_ptr<ApplyInfo>> GetApplyList();
    bool AlreadyApply(int uid);
    void AddApplyList(std::shared_ptr<ApplyInfo>app);
    void SetUserInfo(std::shared_ptr<UserInfo>user_info);
    void AppendApplyList(QJsonArray array);
    void AppendFriendList(QJsonArray array);
     bool CheckFriendById(int uid);
    void AddFriend(std::shared_ptr<AuthRsp> auth_rsp);
    void AddFriend(std::shared_ptr<AuthInfo> auth_info);
    std::shared_ptr<UserInfo> GetFriendById(int uid);

    std::vector<std::shared_ptr<UserInfo>> GetChatListPerPage();
    bool IsLoadChatFin();
    void UpdateChatLoadedCount();
    std::vector<std::shared_ptr<UserInfo>> GetConListPerPage();
    void UpdateContactLoadedCount();
    bool IsLoadConFin();

    //==== 聊天会话（线程）缓存：登录后按 1026 拉到的会话逐个加载消息 ====
    int GetLastChatThreadId();        //翻页游标：上一页最后一条 thread_id（首页传 0）
    void SetLastChatThreadId(int id);
    //登记一个会话：_chat_map 按 thread_id 存，_chat_thread_ids 记录加载顺序，
    //同时把"对方uid → thread_id"也建好映射（跳转聊天时靠它查现成会话）
    void AddChatThreadData(std::shared_ptr<ChatThreadData> chat_thread_data, int other_uid);
    int GetThreadIdByUid(int uid);    //对方的 uid → thread_id（没有返回 -1）
    std::shared_ptr<ChatThreadData> GetChatThreadByThreadId(int thread_id);
    std::shared_ptr<ChatThreadData> GetChatThreadByUid(int uid);

    //登录后逐个会话预加载历史消息：GetCurLoadData 取当前要加载的，
    //加载完调 GetNextLoadData 走到下一个，返回 nullptr 表示全部加载完
    std::shared_ptr<ChatThreadData> GetCurLoadData();
    std::shared_ptr<ChatThreadData> GetNextLoadData();

    //==== 图片传输管理（day39 图片聊天）====
    //唯一文件名 → 图片消息信息：发送方收到 1036 回包后按名字取回 MsgInfo
    //发起 1037 上传；接收方收到 1039 通知时登记占位信息并触发下载
    void AddTransFile(QString name, std::shared_ptr<MsgInfo> msg_info);
    std::shared_ptr<MsgInfo> GetTransFileByName(QString name);
    void RmvTransFile(QString name);
private:
    UserMgr();
    std::shared_ptr<UserInfo> _user_info;
    std::vector<std::shared_ptr<ApplyInfo>> _apply_list;
    std::vector<std::shared_ptr<UserInfo>> _friend_list;
    QMap<int, std::shared_ptr<UserInfo>> _friend_map;
    QString _token;
    int _chat_loaded;
    int _contact_loaded;
    //会话 id → 会话数据 的映射（聊天记录的统一缓存入口）
    QMap<int, std::shared_ptr<ChatThreadData>> _chat_map;
    //会话 id 列表：记录 1026 拉回的顺序，预加载历史消息时按它逐个推进
    std::vector<int> _chat_thread_ids;
    //当前预加载进行到 _chat_thread_ids 的第几个
    int _cur_load_chat_index;
    //上次会话的 id：1025 翻页游标（下一页请求要带上）
    int _last_chat_thread_id;
    //对方 uid → thread_id 的映射，"从联系人/搜索跳转聊天"时先查它
    QMap<int, int> _uid_to_thread_id;
    //传输中的图片消息：唯一文件名 → MsgInfo（1035~1037/1039 全程靠它串联）
    QHash<QString, std::shared_ptr<MsgInfo>> _trans_files;
    //锁：TcpMgr 的回调线程和 UI 线程都可能读写上面的 map，加锁保护
    std::mutex _mtx;
};

#endif // USERMGR_H
