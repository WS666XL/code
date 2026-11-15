#include "conuseritem.h"
#include "ui_conuseritem.h"
#include "global.h"

ConUserItem::ConUserItem(QWidget *parent) :
    ListItemBase(parent),
    ui(new Ui::ConUserItem)
{
    ui->setupUi(this);
    SetItemType(ListItemType::CONTACT_USER_ITEM);
    ui->red_point->raise();
    ShowRedPoint(false);
}

ConUserItem::~ConUserItem()
{
    delete ui;
}

QSize ConUserItem::sizeHint() const
{
    return QSize(250, 70); // 返回自定义的尺寸
}

void ConUserItem::SetInfo(std::shared_ptr<AuthInfo> auth_info)
{
    _info = std::make_shared<UserInfo>(auth_info);
    // 加载图片（走全局缓存；本地缺文件自动下载并刷新，owner_uid 传对方 uid）
    ui->icon_lb->setPixmap(LoadAvatarPixmap(ui->icon_lb, _info->_icon,
        ui->icon_lb->size(), _info->_uid));
    ui->icon_lb->setScaledContents(true);

    ui->user_name_lb->setText(_info->_name);
}

void ConUserItem::SetInfo(int uid, QString name, QString icon)
{
     _info = std::make_shared<UserInfo>(uid,name, name, icon, 0);

     // 加载图片（走全局缓存；本地缺文件自动下载并刷新，owner_uid 传对方 uid）
     ui->icon_lb->setPixmap(LoadAvatarPixmap(ui->icon_lb, _info->_icon,
         ui->icon_lb->size(), _info->_uid));
     ui->icon_lb->setScaledContents(true);

     ui->user_name_lb->setText(_info->_name);
}

void ConUserItem::SetInfo(std::shared_ptr<AuthRsp> auth_rsp){
    _info = std::make_shared<UserInfo>(auth_rsp);

    // 加载图片（走全局缓存；本地缺文件自动下载并刷新，owner_uid 传对方 uid）
    ui->icon_lb->setPixmap(LoadAvatarPixmap(ui->icon_lb, _info->_icon,
        ui->icon_lb->size(), _info->_uid));
    ui->icon_lb->setScaledContents(true);

    ui->user_name_lb->setText(_info->_name);
}

void ConUserItem::ShowRedPoint(bool show)
{
    if(show){
        ui->red_point->show();
    }else{
        ui->red_point->hide();
    }

}

std::shared_ptr<UserInfo> ConUserItem::GetInfo()
{
    return _info;
}
