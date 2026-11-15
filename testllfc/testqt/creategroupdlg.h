#ifndef CREATEGROUPDLG_H
#define CREATEGROUPDLG_H

#include <QDialog>
#include "userdata.h"

/******************************************************************************
 *  文件：creategroupdlg.h
 *  作用：发起群聊对话框（day41 群聊）——
 *        上方群名输入框 + 中间好友多选列表（QListWidget 多选模式）+ 创建按钮
 *  提交：发 1055 {uid, name, member_uids[]}；1056 回包由 chatdialog 处理
 *        （建群条目并跳转），本对话框收到成功信号后关闭自己
 *  限制：只能从好友里选成员（基础版约定，群成员昵称显示走好友表）
 *****************************************************************************/
class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;

class CreateGroupDlg : public QDialog
{
    Q_OBJECT
public:
    explicit CreateGroupDlg(QWidget *parent = nullptr);

private slots:
    void slot_submit();          //点创建：校验后发 1055
    void slot_create_rsp(bool success, int thread_id, QString name, int member_count);

private:
    void loadFriendList();       //把好友列表装进多选列表
    QLineEdit* _name_ed;         //群名输入框
    QListWidget* _friend_list;   //好友多选列表（MultiSelection）
    QPushButton* _submit_btn;    //创建按钮
    QLabel* _tip_lb;             //提示行（至少选一位好友等）
};
#endif // CREATEGROUPDLG_H
