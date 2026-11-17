#include "friendinfopage.h"
#include "Logger.h"
#include "ui_friendinfopage.h"
#include "global.h"
#include <QDebug>

FriendInfoPage::FriendInfoPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FriendInfoPage),_user_info(nullptr)
{
    ui->setupUi(this);
    ui->msg_chat->SetState("normal","hover","press");
    ui->video_chat->SetState("normal","hover","press");
    ui->voice_chat->SetState("normal","hover","press");
}

FriendInfoPage::~FriendInfoPage()
{
    delete ui;
}

void FriendInfoPage::SetInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
    // 加载图片（走全局缓存；本地缺文件自动下载并刷新，owner_uid 传对方 uid）
    ui->icon_lb->setPixmap(LoadAvatarPixmap(ui->icon_lb, user_info->_icon,
        ui->icon_lb->size(), user_info->_uid));
    ui->icon_lb->setScaledContents(true);

    ui->name_lb->setText(user_info->_name);
    //昵称修改已移除：昵称行直接显示用户名，备注行显示自己设置的备注
    ui->nick_lb->setText(user_info->_name);
    ui->bak_lb->setText(user_info->_remark);
}

void FriendInfoPage::on_msg_chat_clicked()
{
    LLOG_DEBUG << "msg chat btn clicked";
    emit sig_jump_chat_item(_user_info);
}
