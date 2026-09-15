#include "logindialog.h"
#include "ui_logindialog.h"

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    ui->setupUi(this);   // 把 .ui 文件里设计的控件全部创建到当前窗口上
    connect(ui->reg_btn, &QPushButton::clicked, this, &LoginDialog::switchRegister);//this是_login_dlg
    // 上面这一行的意思：登录界面的"注册"按钮被点击 → 发出 switchRegister 信号，
    // 信号本身不做切换，真正的界面切换由 MainWindow 的槽函数处理
}

LoginDialog::~LoginDialog()
{
    delete ui;   // ui 是 new 出来的，需要手动释放
}
