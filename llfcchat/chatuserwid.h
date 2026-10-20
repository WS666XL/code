#ifndef CHATUSERWID_H
#define CHATUSERWID_H

#include <QWidget>
#include "listitembase.h"
#include"userdata.h"
namespace Ui {
// 前向声明：由 uic 根据 chatuserwid.ui 自动生成
class ChatUserWid;//聊天用户ID
}

/******************************************************************************
 *  文件：chatuserwid.h
 *  作用：聊天列表里的一条（头像 + 用户名 + 最后一条消息）
 *  注意：它不是 QListWidgetItem，而是普通 QWidget，
 *        由 ChatUserList::setItemWidget() 塞进列表项里显示
 *****************************************************************************/
class ChatUserWid : public ListItemBase
{
    Q_OBJECT

public:
    explicit ChatUserWid(QWidget *parent = nullptr);
    ~ChatUserWid();

    QSize sizeHint() const override {
        return QSize(250, 70); // 返回自定义的尺寸：告诉列表这一项要多高
    }


    void SetInfo(std::shared_ptr<UserInfo> user_info);
    void SetInfo(std::shared_ptr<FriendInfo> friend_info);
    //void ShowRedPoint(bool bshow);
    std::shared_ptr<UserInfo> GetUserInfo();
    void updateLastMsg(std::vector<std::shared_ptr<TextChatData>> msgs);

private:
    Ui::ChatUserWid *ui;  // 界面对象
   std::shared_ptr<UserInfo> _user_info;
};

#endif // CHATUSERWID_H