#ifndef USERMGR_H
#define USERMGR_H
#include <QObject>
#include <memory>
#include <singleton.h>
#include"userdata.h"
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
    QString GetName();
    std::vector<std::shared_ptr<ApplyInfo>> GetApplyList();
private:
    std::vector<std::shared_ptr<ApplyInfo>> _apply_list;
    UserMgr();          // 私有，外部不能 new，只能走 GetInstance()
    QString _name;      // 用户名
    QString _token;     // 登录令牌
    int _uid;           // 用户 id

};

#endif // USERMGR_H
