#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include"logindialog.h"
#include"registerdialog.h"
#include"resetdialog.h"
#include <QMainWindow>

/******************************************************************************
 *
 * @file       mainwindow.h
 * @brief      XXXX Function
 *
 * @author     许磊大王
 * @date       2026/08/11
 * @history
 *****************************************************************************/
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// 主窗口：本身只是一个"容器"，真正的界面是登录/注册两个对话框
// 切换方式：把某个对话框设为 CentralWidget(中心部件)，就实现了界面切换
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
public slots:
    void SlotSwitchReg();   // 收到登录界面的 switchRegister 信号后，把中心部件换成注册界面
    void SlotSwitchLogin();     // 从注册界面返回登录界面
    void SlotSwitchReset();     // 切到忘记密码（重置密码）界面
    void SlotSwitchLogin2();    // 从重置密码界面返回登录界面
private:
    Ui::MainWindow *ui;           // 主窗口自身的 UI（只是个空容器，真正的界面是三个对话框）
    LoginDialog * _login_dlg;      // 登录界面
    RegisterDialog* _reg_dlg;    // 注册界面
    ResetDialog*_reset_dlg;        // 重置密码界面
};
#endif // MAINWINDOW_H
