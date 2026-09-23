#include "logindialog.h"
#include "ui_logindialog.h"
#include<qDebug>
LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    ui->setupUi(this);   // 把 .ui 文件里设计的控件全部创建到当前窗口上
    connect(ui->reg_btn, &QPushButton::clicked, this, &LoginDialog::switchRegister);//this是_login_dlg
    // 上面这一行的意思：登录界面的"注册"按钮被点击 → 发出 switchRegister 信号，
    // 信号本身不做切换，真正的界面切换由 MainWindow 的槽函数处理
    ui->forget_label->SetState("normal","hover","","selected","selected_hover","");

    connect(ui->forget_label, &ClickedLabel::clicked, this, &LoginDialog::slot_forget_pwd);
}

LoginDialog::~LoginDialog()
{
    qDebug()<<"~LoginDialog()";
    delete ui;   // ui 是 new 出来的，需要手动释放
}

void LoginDialog::slot_forget_pwd()
{
    qDebug()<<"slot forget pwd";
    emit switchReset();
}
