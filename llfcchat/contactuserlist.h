#ifndef CONTACTUSERLIST_H
#define CONTACTUSERLIST_H
#include <QListWidget>
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QDebug>
class ConUserItem;

class ContactUserList : public QListWidget//联系人列表
{
    Q_OBJECT
public:
    ContactUserList(QWidget *parent = nullptr);
    void ShowRedPoint(bool bshow = true);//设置红点
protected:
    bool eventFilter(QObject *watched, QEvent *event) override ;

private:
    void addContactUserList();

public slots:
    void slot_item_clicked(QListWidgetItem *item);
    //     void slot_add_auth_firend(std::shared_ptr<AuthInfo>);
    //     void slot_auth_rsp(std::shared_ptr<AuthRsp>);
signals:
    // 请求加载联系人
    void sig_loading_contact_user();

    // 切换到好友申请页面
    void sig_switch_apply_friend_page();

    // 切换到好友信息页面
    void sig_switch_friend_info_page();
private:
    // “添加好友”列表项
    ConUserItem* _add_friend_item;

    // 联系人分组列表项
    QListWidgetItem* _groupitem;
};

#endif // CONTACTUSERLIST_H
