#include "applyfrienditem.h"
#include "ui_applyfrienditem.h"
#include "global.h"

ApplyFriendItem::ApplyFriendItem(QWidget *parent) :
    ListItemBase(parent), ui(new Ui::ApplyFriendItem),
    _added(false)
{
    ui->setupUi(this);
    SetItemType(ListItemType::APPLY_FRIEND_ITEM);
    ui->addBtn->SetState("normal","hover", "press");
    ui->addBtn->hide();
    connect(ui->addBtn, &ClickedBtn::clicked,  [this](){
        emit this->sig_auth_friend(_apply_info);
    });
}

ApplyFriendItem::~ApplyFriendItem()
{
    delete ui;
}

void ApplyFriendItem::SetInfo(std::shared_ptr<ApplyInfo> apply_info)
{
    _apply_info = apply_info;
    // 加载图片（走全局缓存，避免同一头像反复解码+缩放）
    ui->icon_lb->setPixmap(LoadAvatarPixmap(_apply_info->_icon, ui->icon_lb->size()));
    ui->icon_lb->setScaledContents(true);

    ui->user_name_lb->setText(_apply_info->_name);
    ui->user_chat_lb->setText(_apply_info->_desc);
}

void ApplyFriendItem::ShowAddBtn(bool bshow)
{
    if (bshow) {
        ui->addBtn->show();
        ui->already_add_lb->hide();
        _added = false;
    }
    else {
        ui->addBtn->hide();
        ui->already_add_lb->show();
        _added = true;
    }
}

int ApplyFriendItem::GetUid() {
    return _apply_info->_uid;
}