#include "chatitembase.h"
#include "usermgr.h"

ChatItemBase::ChatItemBase(ChatRole rple, QWidget *parent):QWidget(parent),m_role(rple)
{
    m_pNameLabel    = new QLabel();
    m_pNameLabel->setObjectName("chat_user_name");
    QFont font("Microsoft YaHei");
    font.setPointSize(9);
    m_pNameLabel->setFont(font);
    m_pNameLabel->setFixedHeight(20);
    m_pIconLabel  = new QLabel();
    m_pIconLabel->setScaledContents(true);
    m_pIconLabel->setFixedSize(42, 42);
    m_pBubble       = new QWidget();

    //状态图标：自己发的消息旁边显示 未读/发送失败/已读 小图标
    m_pStatusLabel = new QLabel();
    m_pStatusLabel->setFixedSize(16,16);
    m_pStatusLabel->setScaledContents(true);

    QGridLayout *pGLayout = new QGridLayout();
    pGLayout->setVerticalSpacing(3);
    pGLayout->setHorizontalSpacing(3);
    pGLayout->setContentsMargins(3,3,3,3);
    QSpacerItem*pSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    if(m_role == ChatRole::Self)
    {
        m_pNameLabel->setContentsMargins(0,0,8,0);
        m_pNameLabel->setAlignment(Qt::AlignRight);
        pGLayout->addWidget(m_pNameLabel, 0,2, 1,1);
        pGLayout->addWidget(m_pIconLabel, 0, 3, 2,1, Qt::AlignTop);
        pGLayout->addItem(pSpacer, 1, 0, 1, 1);
        pGLayout->addWidget(m_pStatusLabel,1,1,1,1,Qt::AlignCenter);
        pGLayout->addWidget(m_pBubble, 1,2, 1,1);
        pGLayout->setColumnStretch(0, 2);
        pGLayout->setColumnStretch(1,0); //状态图标（固定大小，不拉伸）
        pGLayout->setColumnStretch(2,3); //名字+气泡（主要拉伸区域）
        pGLayout->setColumnStretch(3, 0);
    }else{
        m_pNameLabel->setContentsMargins(8,0,0,0);
        m_pNameLabel->setAlignment(Qt::AlignLeft);
        pGLayout->addWidget(m_pIconLabel, 0, 0, 2,1, Qt::AlignTop);
        pGLayout->addWidget(m_pNameLabel, 0,1, 1,1);
        pGLayout->addWidget(m_pBubble, 1,1, 1,1);
        pGLayout->addItem(pSpacer, 2, 2, 1, 1);
        pGLayout->setColumnStretch(1, 3);
        pGLayout->setColumnStretch(2, 2);
    }
    this->setLayout(pGLayout);
}

void ChatItemBase::setUserName(const QString &name)
{
    m_pNameLabel->setText(name);
}

void ChatItemBase::setUserIcon(const QPixmap &icon)
{
    m_pIconLabel->setPixmap(icon);
}

//头像变更实时刷新（1057）：按消息发送者 uid 重新加载头像。
//用带 label 的 LoadAvatarPixmap 重载——缓存文件被删后这里会触发 1033 下载
//并把本条气泡的头像 label 注册进待刷新表，下载完成自动回显新头像
void ChatItemBase::RefreshAvatar(int owner_uid)
{
    QString icon;
    if (owner_uid <= 0 || owner_uid == UserMgr::GetInstance()->GetUid()) {
        auto self_info = UserMgr::GetInstance()->GetUserInfo();
        if (self_info == nullptr) {
            return;
        }
        icon = self_info->_icon;
        owner_uid = UserMgr::GetInstance()->GetUid();
    }
    else {
        auto friend_info = UserMgr::GetInstance()->GetFriendById(owner_uid);
        if (friend_info == nullptr || friend_info->_icon.isEmpty()) {
            return;
        }
        icon = friend_info->_icon;
    }
    m_pIconLabel->setPixmap(
        LoadAvatarPixmap(m_pIconLabel, icon, m_pIconLabel->size(), owner_uid));
}

void ChatItemBase::setWidget(QWidget *w)
{
    QGridLayout *pGLayout = (qobject_cast<QGridLayout *>)(this->layout());
    pGLayout->replaceWidget(m_pBubble, w);
    delete m_pBubble;
    m_pBubble = w;
}

//按 MsgStatus 切状态小图标：0 未读 / 1 发送失败 / 2 已读
void ChatItemBase::setStatus(int status)
{
    if(status == MsgStatus::UN_READ){
        m_pStatusLabel->setPixmap(QPixmap(":/res/unread.png"));
        return ;
    }

    if(status == MsgStatus::SEND_FAILED){
        m_pStatusLabel->setPixmap(QPixmap(":/res/send_fail.png"));
        return ;
    }

    if(status == MsgStatus::READED){
        m_pStatusLabel->setPixmap(QPixmap(":/res/readed.png"));
        return ;
    }
}

QLabel* ChatItemBase::getIconLabel() {
    return m_pIconLabel;
}

QWidget* ChatItemBase::getBubble() {
    return m_pBubble;
}
