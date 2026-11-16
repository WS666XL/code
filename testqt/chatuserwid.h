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
 *  数据：直接持有 ChatThreadData（会话数据），名字/头像从对方 UserInfo 取，
 *        最后一条消息从 ChatThreadData::GetLastMsg() 取
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

    //绑定会话数据并刷新显示（替代旧的 SetInfo(UserInfo/FriendInfo)）
    void SetChatData(std::shared_ptr<ChatThreadData> chat_data);
    std::shared_ptr<ChatThreadData> GetChatData();   //点击列表项时取会话数据跳转聊天页
    //收到新消息后刷新"最后一条消息"文字（顺带把消息归档进会话缓存）
    void updateLastMsg(std::vector<std::shared_ptr<TextChatData>> msgs);

private:
    Ui::ChatUserWid *ui;  // 界面对象
   std::shared_ptr<ChatThreadData> _chat_data;
};

#endif // CHATUSERWID_H
