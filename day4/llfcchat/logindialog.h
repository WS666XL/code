#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H
#include"global.h"
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
public slots:
    void slot_forget_pwd();   // "忘记密码"按钮的槽：通知 MainWindow 切到重置密码界面
private slots:
    void on_login_btn_clicked();
    void slot_login_mod_finish(ReqId id, QString res, ErrorCodes err);

private:
    void initHttpHandlers(); //注册回调函数
    void initHead();
    Ui::LoginDialog *ui;    // 指向自动生成的界面对象，通过 ui->控件名 访问界面上的控件
    bool checkUserValid();
    bool checkPwdValid();
    void AddTipErr(TipErr te, QString tips);   // 记录一条错误提示并立即显示
    void DelTipErr(TipErr te);    // 解除某类错误提示（没有其它错误就清空提示栏）
    void showTip(QString str,bool b_ok);//展示错误
    bool enableBtn(bool enabled);//设置按钮是否有效
private:
    QMap<TipErr, QString> _tip_errs;   // 当前待显示的错误提示集合（避免多条提示互相覆盖）
    QMap<ReqId,std::function<void(const QJsonObject&)>>_handlers;
    int _uid;
    QString _token;
signals:
    void switchRegister(); // 自定义信号：点了"注册"按钮时发出，由 MainWindow 接收并切换界面
    void switchReset();    // 自定义信号：点了"忘记密码"时发出，由 MainWindow 接收并切到重置密码界面
    void sig_connect_tcp(ServerInfo);//发给长链接的信号
};

#endif // LOGINDIALOG_H
