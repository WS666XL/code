#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

// 前向声明：ui_logindialog.h 编译时自动生成，这里先声明一下类型即可
namespace Ui {
class LoginDialog;
}

// 登录对话框：继承 QDialog，界面由 logindialog.ui 描述
class LoginDialog : public QDialog
{
    Q_OBJECT   // 只要类里要用信号/槽，就必须写这个宏

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();
private slots:
    void slot_forget_pwd();   // "忘记密码"按钮的槽：通知 MainWindow 切到重置密码界面
private:
    Ui::LoginDialog *ui;   // 指向自动生成的界面对象，通过 ui->控件名 访问界面上的控件
signals:
    void switchRegister(); // 自定义信号：点了"注册"按钮时发出，由 MainWindow 接收并切换界面
    void switchReset();    // 自定义信号：点了"忘记密码"时发出，由 MainWindow 接收并切到重置密码界面
};

#endif // LOGINDIALOG_H
