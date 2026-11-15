#include "chatuserwid.h"
#include "ui_chatuserwid.h"
#include "global.h"
#include "usermgr.h"
#include <QTime>

ChatUserWid::ChatUserWid(QWidget *parent) :
    ListItemBase(parent),
    ui(new Ui::ChatUserWid)
{
    ui->setupUi(this);
    SetItemType(ListItemType::CHAT_USER_ITEM);
}

ChatUserWid::~ChatUserWid()
{
    delete ui;
}

void ChatUserWid::SetChatData(std::shared_ptr<ChatThreadData> chat_data) {
    _chat_data = chat_data;
    if (!_chat_data) {
        return;
    }

    //私聊：根据会话里存的"对方uid"查对方资料，取名字和头像
    auto other_id = _chat_data->GetOtherId();
    auto other_info = UserMgr::GetInstance()->GetFriendById(other_id);
    //时间戳：微信风格（今天→HH:mm / 昨天 / 星期X / 日期），来源于最后一条消息
    ui->label->setText(_chat_data->GetLastMsgTime());

    //对方资料查不到（比如异常数据）：显示 uid 兜底，头像用默认图，避免空指针崩溃
    if (!other_info) {
        ui->user_name_lb->setText(QString::number(other_id));
        ui->icon_lb->setPixmap(LoadAvatarPixmap(":/res/head_1.jpg", ui->icon_lb->size()));
        ui->icon_lb->setScaledContents(true);
        ui->user_chat_lb->setText(_chat_data->GetLastMsg());
        return;
    }

    // 加载图片（走全局缓存，避免同一头像反复解码+缩放）
    ui->icon_lb->setPixmap(LoadAvatarPixmap(other_info->_icon, ui->icon_lb->size()));
    ui->icon_lb->setScaledContents(true);

    ui->user_name_lb->setText(other_info->_name);
    //最后一条消息来自会话缓存（1029 预加载历史时会不断刷新）
    ui->user_chat_lb->setText(_chat_data->GetLastMsg());
}

std::shared_ptr<ChatThreadData> ChatUserWid::GetChatData()
{
    return _chat_data;
}

void ChatUserWid::updateLastMsg(std::vector<std::shared_ptr<TextChatData>> msgs) {

    if (!_chat_data) {
        return;
    }

    QString last_msg = "";
    int last_msg_id = 0;
    for (auto& msg : msgs) {
        last_msg = msg->GetContent();
        last_msg_id = msg->GetMsgId();
        _chat_data->AddMsg(msg);
    }

    _chat_data->SetLastMsgId(last_msg_id);
    ui->user_chat_lb->setText(last_msg);
}
