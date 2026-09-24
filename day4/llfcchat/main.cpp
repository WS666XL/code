#include "mainwindow.h"

#include <QApplication>
#include<QFile>
#include"global.h"

// 程序入口
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);          // 每个 Qt GUI 程序都必须有一个 QApplication
    QFile qss(":/style/stylesheet.qss"); // 从资源文件(qrc)里读取样式表

    if( qss.open(QFile::ReadOnly))//只读
    {
        qDebug("open success");
        QString style = QLatin1String(qss.readAll());
        a.setStyleSheet(style);          // 全局套用样式，所有控件都会受影响
        qss.close();
    }else{
        qDebug("Open failed");
    }

    // ===== 读取 config.ini，拼出后端服务器地址前缀 =====
    // 获取当前应用程序的路径
    // (注意：拿到的是 exe 所在目录，所以 config.ini 必须和 exe 放在一起
    //  —— 这也是 llfcchat.pro 里要用 QMAKE_POST_LINK 把它拷过去的原因)
    QString app_path = QCoreApplication::applicationDirPath();
    // 拼接文件名
    QString fileName = "config.ini";
    // QDir::toNativeSeparators 把 "/" 换成 Windows 的 "\"，
    // 保证拼出来的路径在 Windows 下也能被正确识别
    //QDir::separator()它返回当前平台的目录分隔符。
    QString config_path = QDir::toNativeSeparators(app_path +
                                                   QDir::separator() + fileName);

    // QSettings 以 ini 格式读配置：ini 里的 [GateServer] + host=xxx
    // 取值时写成 "GateServer/host"（节名/键名）
    QSettings settings(config_path, QSettings::IniFormat);
    QString gate_host = settings.value("GateServer/host").toString();
    QString gate_port = settings.value("GateServer/port").toString();
    // 拼成 "http://localhost:8080" 存进全局变量，各界面再用它拼具体接口
    gate_url_prefix = "http://"+gate_host+":"+gate_port;
    MainWindow w;                        // 创建主窗口（它内部再创建登录/注册对话框）
    w.show();                            // 显示窗口
    return QCoreApplication::exec();     // 进入事件循环，程序在这里一直"挂着"直到窗口关闭
}
