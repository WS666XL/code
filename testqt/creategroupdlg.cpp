#include "creategroupdlg.h"
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "usermgr.h"
#include "tcpmgr.h"

CreateGroupDlg::CreateGroupDlg(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("发起群聊"));
    setFixedSize(360, 480);

    auto* v_layout = new QVBoxLayout(this);
    v_layout->setContentsMargins(20, 20, 20, 20);
    v_layout->setSpacing(10);

    //群名输入行
    auto* name_layout = new QHBoxLayout();
    name_layout->addWidget(new QLabel(QStringLiteral("群名："), this));
    _name_ed = new QLineEdit(this);
    _name_ed->setPlaceholderText(QStringLiteral("请输入群名"));
    name_layout->addWidget(_name_ed);
    v_layout->addLayout(name_layout);

    //提示行
    _tip_lb = new QLabel(QStringLiteral("选择群成员（可多选）："), this);
    _tip_lb->setStyleSheet("color:#999; font-size:12px;");
    v_layout->addWidget(_tip_lb);

    //好友多选列表
    _friend_list = new QListWidget(this);
    _friend_list->setSelectionMode(QAbstractItemView::MultiSelection);
    v_layout->addWidget(_friend_list, 1);
    loadFriendList();

    //创建按钮
    _submit_btn = new QPushButton(QStringLiteral("创建"), this);
    _submit_btn->setFixedHeight(34);
    v_layout->addWidget(_submit_btn);

    connect(_submit_btn, &QPushButton::clicked, this, &CreateGroupDlg::slot_submit);
    //1056 回包：成功就关掉对话框（建群条目由 chatdialog 建并跳转）
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_create_group_rsp,
            this, &CreateGroupDlg::slot_create_rsp);
}

//把当前登录用户的好友装进多选列表（显示昵称，data 存 uid）
void CreateGroupDlg::loadFriendList()
{
    auto friends = UserMgr::GetInstance()->GetFriendListSnapshot();
    for (auto& friend_info : friends) {
        auto* item = new QListWidgetItem(friend_info->DisplayName());
        item->setData(Qt::UserRole, friend_info->_uid);
        _friend_list->addItem(item);
    }
}

//点创建：群名非空 + 至少选一位好友 → 发 1055
void CreateGroupDlg::slot_submit()
{
    auto name = _name_ed->text().trimmed();
    if (name.isEmpty()) {
        _tip_lb->setText(QStringLiteral("请输入群名"));
        return;
    }

    QJsonArray member_uids;
    for (auto* item : _friend_list->selectedItems()) {
        member_uids.append(item->data(Qt::UserRole).toInt());
    }
    if (member_uids.isEmpty()) {
        _tip_lb->setText(QStringLiteral("至少选择一位好友"));
        return;
    }

    QJsonObject jsonObj;
    jsonObj["uid"] = UserMgr::GetInstance()->GetUid();
    jsonObj["name"] = name;
    jsonObj["member_uids"] = member_uids;
    QJsonDocument doc(jsonObj);
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_CREATE_GROUP_REQ,
        doc.toJson(QJsonDocument::Compact));

    _submit_btn->setEnabled(false);
    _tip_lb->setText(QStringLiteral("创建中..."));
}

void CreateGroupDlg::slot_create_rsp(bool success, int thread_id,
                                     QString name, int member_count)
{
    Q_UNUSED(thread_id);
    Q_UNUSED(name);
    Q_UNUSED(member_count);
    if (success) {
        accept();   //关闭对话框（建群条目由 chatdialog 建）
    }
    else {
        _submit_btn->setEnabled(true);
        _tip_lb->setText(QStringLiteral("创建失败，请重试"));
    }
}
