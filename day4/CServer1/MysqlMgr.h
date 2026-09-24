#pragma once
#include "const.h"
#include "MysqlDao.h"
// MysqlMgr —— MySQL 逻辑层单例：业务代码只认它，实际读写交给内部的 MysqlDao
class MysqlMgr : public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;
public:
    ~MysqlMgr();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);   // 注册用户：返回新 uid；0 = 用户名或邮箱已存在，-1 = 出错已回滚
    bool CheckEmail(const std::string& name, const std::string& email);                       // 校验"用户名 + 邮箱"是否匹配（改密码前验证身份用）
    bool UpdatePwd(const std::string& name, const std::string& email);                        // 按用户名更新密码，成功返回 true
    bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
private:
    MysqlMgr();
    MysqlDao  _dao;      // 数据访问对象，连接池在它内部
};
