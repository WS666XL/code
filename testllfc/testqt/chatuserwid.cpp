#include "chatuserwid.h"
#include "ui_chatuserwid.h"
#include "global.h"
#include <QTime>

ChatUserWid::ChatUserWid(QWidget *parent) :
    ListItemBase(parent),
    ui(new Ui::ChatUserWid)
{
    ui->setupUi(this);
    SetItemType(ListItemType::CHAT_USER_ITEM);
    //自己加的
    // this->setStyleSheet(R"(
    //     ChatUserWid {
    //         background-color: #FFFFFF;
    //         border-radius: 8px;
    //     }

    //     ChatUserWid:hover {
    //         background-color: #E6F0FF;
    //     }


    // )");
}

ChatUserWid::~ChatUserWid()
{
    delete ui;
}


void ChatUserWid::SetInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
    // 加载图片（走全局缓存，避免同一头像反复解码+缩放）
    ui->icon_lb->setPixmap(LoadAvatarPixmap(_user_info->_icon, ui->icon_lb->size()));
    ui->icon_lb->setScaledContents(true);

    ui->user_name_lb->setText(_user_info->_name);
    ui->user_chat_lb->setText(_user_info->_last_msg);
    // 显示当前时间（.ui 里默认是占位文字"时间"，教程代码一直没更新它）
    ui->label->setText(QTime::currentTime().toString("hh:mm"));
}

void ChatUserWid::SetInfo(std::shared_ptr<FriendInfo> friend_info)
{
    if (!friend_info) {
        return;
    }
    _user_info = std::make_shared<UserInfo>(friend_info);
    // 加载图片（走全局缓存，避免同一头像反复解码+缩放）
    ui->icon_lb->setPixmap(LoadAvatarPixmap(_user_info->_icon, ui->icon_lb->size()));
    ui->icon_lb->setScaledContents(true);

    ui->user_name_lb->setText(_user_info->_name);
    ui->user_chat_lb->setText(_user_info->_last_msg);
    ui->label->setText(QTime::currentTime().toString("hh:mm"));
}
// void ChatUserWid::ShowRedPoint(bool bshow)
// {
//     if(bshow){
//         ui->red_point->show();
//     }else{
//         ui->red_point->hide();
//     }
// }
void ChatUserWid::updateLastMsg(std::vector<std::shared_ptr<TextChatData>> msgs) {

    QString last_msg = "";
    for (auto& msg : msgs) {
        last_msg = msg->_msg_content;
        _user_info->_chat_msgs.push_back(msg);
    }

    _user_info->_last_msg = last_msg;
    ui->user_chat_lb->setText(_user_info->_last_msg);
}
std::shared_ptr<UserInfo> ChatUserWid::GetUserInfo()
{
    return _user_info;
}

