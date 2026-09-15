#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    //创建一个CentralWidget, 并将其设置为MainWindow的中心部件
    // 先把登录界面作为主窗口的中心部件，用户看到的就是登录页
    _login_dlg = new LoginDialog(this);
    setCentralWidget(_login_dlg); //mainwindow.ui有个CentralWidget
    _login_dlg->show();


    //创建和注册消息的链接
    // 登录界面点"注册" → 主窗口的 SlotSwitchReg 槽函数被调用
    connect(_login_dlg, &LoginDialog::switchRegister,
            this, &MainWindow::SlotSwitchReg);
    _reg_dlg = new RegisterDialog(this);

    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    _reg_dlg->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);//setCentralWidget
    //是把 Login 界面内嵌到主窗口内部区域，你又给内嵌的这个子控件强行加了顶层窗口无边框标志
    // 效果：去掉子窗口自带的边框/标题栏，让它看起来像主窗口的一部分；注册界面先隐藏，等切换时再显示
    _reg_dlg->hide();

}

MainWindow::~MainWindow()
{
    delete ui;
    // 下面这些原本是手动释放子界面的代码，因为 _login_dlg / _reg_dlg 的父对象都是 this，
    // 主窗口析构时会自动连带释放，所以被注释掉了
    // if(_login_dlg)
    // {
    //     _login_dlg->deleteLater();
    //     _login_dlg=nullptr;
    // }
    // if(_reg_dlg)
    // {
    //     _reg_dlg->deleteLater();
    //     _reg_dlg=nullptr;
    // }
}

// 切换界面：把中心部件从登录界面换成注册界面
void MainWindow::SlotSwitchReg()
{
    setCentralWidget(_reg_dlg);   // 同一个时刻，中心部件只能有一个
    _login_dlg->hide();
    _reg_dlg->show();
}
