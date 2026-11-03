#include "mainwindow.h"
#include "ui_mainwindow.h"
#include"tcpmgr.h"
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWindow>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , _cur_center(nullptr)
{
    ui->setupUi(this);
    // Linux/WSLg 的系统标题栏又丑又慢（点 X 关闭有延迟）：
    // 整个窗口改为无边框，用自己画的标题栏（图标 + 标题 + 最小化/最大化/关闭）
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    // content_holder 里放一个布局，用来承载登录/注册/聊天等界面
    _content_layout = new QVBoxLayout(ui->content_holder);
    _content_layout->setContentsMargins(0, 0, 0, 0);
    _content_layout->setSpacing(0);

    // 标题栏图标 + 拖动事件
    ui->title_icon->setPixmap(QPixmap(":/res/icon.png"));
    ui->title_bar->installEventFilter(this);

    // 标题栏三个按钮：最小化 / 最大化还原 / 关闭
    connect(ui->btn_min, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(ui->btn_max, &QToolButton::clicked, this, &MainWindow::SlotToggleMax);
    connect(ui->btn_close, &QToolButton::clicked, this, &QWidget::close);

    //创建一个内容页, 先把登录界面作为内容页，用户看到的就是登录页
    _login_dlg = new LoginDialog(this);
    SetCenterWidget(_login_dlg);

    //创建和注册消息的链接
    // 登录界面点"注册" → 主窗口的 SlotSwitchReg 槽函数被调用
    connect(_login_dlg, &LoginDialog::switchRegister,
            this, &MainWindow::SlotSwitchReg);
    //连接登录界面忘记密码信号
    connect(_login_dlg, &LoginDialog::switchReset, this, &MainWindow::SlotSwitchReset);

    //连接创建聊天界面信号
    connect(TcpMgr::GetInstance().get(),&TcpMgr::sig_swich_chatdlg, this, &MainWindow::SlotSwitchChat);

    //emit TcpMgr::GetInstance()->sig_swich_chatdlg();

}

MainWindow::~MainWindow()
{
    delete ui;

}

// 切换内容页：旧的先隐藏并 deleteLater，再把新的塞进内容区布局
// （原教程用 setCentralWidget 会直接 delete 旧界面，随后又访问旧指针，是 use-after-free）
void MainWindow::SetCenterWidget(QWidget *w)
{
    if (_cur_center) {
        _cur_center->hide();
        _cur_center->deleteLater();
    }
    _content_layout->addWidget(w);
    w->show();
    _cur_center = w;
}

// 标题栏按住拖动窗口
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->title_bar && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && windowHandle()) {
            windowHandle()->startSystemMove();   // 交给系统移动窗口
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// 最大化 / 还原切换
void MainWindow::SlotToggleMax()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

// 切换界面：把内容区换成注册界面
void MainWindow::SlotSwitchReg()
{
    _reg_dlg = new RegisterDialog(this);

    //连接注册界面返回登录信号
    connect(_reg_dlg, &RegisterDialog::sigSwitchLogin, this, &MainWindow::SlotSwitchLogin);
    SetCenterWidget(_reg_dlg);
}

//从注册界面返回登录界面
void MainWindow::SlotSwitchLogin()
{
    //创建一个内容页，把内容区换回登录界面
    _login_dlg = new LoginDialog(this);
    SetCenterWidget(_login_dlg);

    //连接登录界面注册信号
    connect(_login_dlg, &LoginDialog::switchRegister, this, &MainWindow::SlotSwitchReg);
    //连接登录界面忘记密码信号
    connect(_login_dlg, &LoginDialog::switchReset, this, &MainWindow::SlotSwitchReset);//我自己加的

}

void MainWindow::SlotSwitchReset()
{
    //创建一个内容页，把内容区换成重置密码界面
    _reset_dlg = new ResetDialog(this);
    SetCenterWidget(_reset_dlg);

    //注册返回登录信号和槽函数
    connect(_reset_dlg, &ResetDialog::switchLogin, this, &MainWindow::SlotSwitchLogin2);
}

void MainWindow::SlotSwitchLogin2()
{
    //创建一个内容页，把内容区换回登录界面
    _login_dlg = new LoginDialog(this);
    SetCenterWidget(_login_dlg);

    //连接登录界面注册信号
    connect(_login_dlg, &LoginDialog::switchRegister, this, &MainWindow::SlotSwitchReg);
    //连接登录界面忘记密码信号
    connect(_login_dlg, &LoginDialog::switchReset, this, &MainWindow::SlotSwitchReset);
}

void MainWindow::SlotSwitchChat()
{
    _chat_dlg = new ChatDialog();
    SetCenterWidget(_chat_dlg);      // 将聊天界面换入内容区
    this->setMinimumSize(QSize(1050,900));
    this->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}
