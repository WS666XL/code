#include "findsuccessdlg.h"
#include "ui_findsuccessdlg.h"
#include "global.h"
#include"applyfriend.h"
#include<QDir>
FindSuccessDlg::FindSuccessDlg(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::FindSuccessDlg),_parent(parent)
{
    ui->setupUi(this);
    // 设置对话框标题
    setWindowTitle("添加");
    // 隐藏对话框标题栏
    //setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    // 获取当前应用程序的路径
    QString app_path = QCoreApplication::applicationDirPath();
    QString pix_path = QDir::toNativeSeparators(app_path +
                                                QDir::separator() + "static"+QDir::separator()+"head_1.jpg");//测试用
    QPixmap head_pix(pix_path);
    head_pix = head_pix.scaled(ui->head_lb->size(),
                               Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->head_lb->setPixmap(head_pix);
    ui->add_friend_btn->SetState("normal","hover","press");
    this->setModal(true);
}

FindSuccessDlg::~FindSuccessDlg()
{
    qDebug()<<"FindSuccessDlg destruct";
    delete ui;
}

void FindSuccessDlg::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{
    ui->name_lb->setText(si->_name);
    // 显示搜索到的用户头像（教程代码漏了这一步，head_lb 一直是空的）
    ui->head_lb->setPixmap(LoadAvatarPixmap(si->_icon, ui->head_lb->size()));
    ui->head_lb->setScaledContents(true);
    _si = si;
}

void FindSuccessDlg::on_add_friend_btn_clicked()
{
    //todo... 添加好友界面弹出
    this->hide();
    //弹出加好友界面
    auto applyFriend = new ApplyFriend(_parent);
    applyFriend->SetSearchInfo(_si);
    applyFriend->setModal(true);
    applyFriend->show();
}