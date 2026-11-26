#include "mainwindow.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QHash>
#include "global.h"
#include "Logger.h"

#ifdef Q_OS_LINUX
// WSLg X11(xcb) 下模态 QDialog 的灰色标题栏由 Weston 渲染，缺少中文字体，中文标题会显示成方块。
// 仅在 WSL 环境下自动把 QDialog 标题映射成英文（保留 MainWindow 的“轻讯”原生标题，不影响 Windows/原生桌面 Linux）。
class WslDialogTitleFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::WindowTitleChange) {
            if (auto* dlg = qobject_cast<QDialog*>(obj)) {
                const QString title = dlg->windowTitle();
                if (HasNonAscii(title)) {
                    dlg->setWindowTitle(MapTitle(title));
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }

private:
    static bool HasNonAscii(const QString& s) {
        for (const QChar& ch : s) {
            if (ch.unicode() >= 128) {
                return true;
            }
        }
        return false;
    }

    static QString MapTitle(const QString& zh) {
        static const QHash<QString, QString> kMap = {
            {QStringLiteral("设置百炼 API Key"), QStringLiteral("Set AI API Key")},
            {QStringLiteral("设置备注"), QStringLiteral("Set Remark")},
            {QStringLiteral("删除好友"), QStringLiteral("Delete Friend")},
            {QStringLiteral("删除失败"), QStringLiteral("Delete Failed")},
            {QStringLiteral("设置备注失败"), QStringLiteral("Set Remark Failed")},
            {QStringLiteral("删除好友失败"), QStringLiteral("Delete Friend Failed")},
            {QStringLiteral("发起群聊"), QStringLiteral("Create Group")},
            {QStringLiteral("图片预览"), QStringLiteral("Image Preview")},
            {QStringLiteral("选择文件"), QStringLiteral("Select File")},
            {QStringLiteral("选择图片"), QStringLiteral("Select Image")},
            {QStringLiteral("另存为"), QStringLiteral("Save As")},
            {QStringLiteral("添加"), QStringLiteral("Add Friend")},
            {QStringLiteral("提示"), QStringLiteral("Notice")},
            {QStringLiteral("下线提示"), QStringLiteral("Offline Notice")},
            {QStringLiteral("保存失败"), QStringLiteral("Save Failed")},
            {QStringLiteral("保存成功"), QStringLiteral("Saved")},
            {QStringLiteral("上传失败"), QStringLiteral("Upload Failed")},
            {QStringLiteral("错误"), QStringLiteral("Error")},
        };
        auto it = kMap.find(zh);
        return it != kMap.end() ? it.value() : QStringLiteral("QingXun");
    }
};
#endif

// 程序入口
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);          // 每个 Qt GUI 程序都必须有一个 QApplication
    a.setApplicationName("qingxun");
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsSet("WSL_DISTRO_NAME")) {
        a.installEventFilter(new WslDialogTitleFilter(&a));
    }
#endif
    // 主窗口标题已在 mainwindow.ui 设为“轻讯”；不设 applicationDisplayName，
    // 避免 Qt 自动给子弹窗标题拼上“— 轻讯”后缀在 WSLg X11 标题栏显示为方块字
    // 日志：控制台 + <程序目录>/logs/client.log
    xulei::Logger::Init("client",
        (QCoreApplication::applicationDirPath() + "/logs").toStdString(), "debug");
    // 应用底色调成白色：窗口刚映射、Qt 还没画出第一帧时不会闪黑屏
    a.setPalette(QColor(255, 255, 255));
    QFile qss(":/style/stylesheet.qss"); // 从资源文件(qrc)里读取样式表

    if( qss.open(QFile::ReadOnly))//只读
    {
        LLOG_INFO << "open success";
        QString style = QLatin1String(qss.readAll());
        a.setStyleSheet(style);          // 全局套用样式，所有控件都会受影响
        qss.close();
    }else{
        LLOG_INFO << "Open failed";
    }

    // ===== 读取 config.ini，拼出后端服务器地址前缀 =====
    // 获取当前应用程序的路径
    // (注意：拿到的是 exe 所在目录，所以 config.ini 必须和 exe 放在一起
    //  —— 这也是 qingxun.pro 里要用 QMAKE_POST_LINK 把它拷过去的原因)
    QString app_path = QCoreApplication::applicationDirPath();
    // 拼接文件名
    QString fileName = "config.ini";
    // QDir::toNativeSeparators 把 "/" 换成 Windows 的 "\"，
    // 保证拼出来的路径在 Windows 下也能被正确识别
    //QDir::separator()它返回当前平台的目录分隔符。
    QString config_path = QDir::toNativeSeparators(app_path +
                                                   QDir::separator() + fileName);

    // QSettings 以 ini 格式读配置：优先读 [ApiGateway]，兼容旧的 [GateServer]
    QSettings settings(config_path, QSettings::IniFormat);
    // 日志级别可在 config.ini 的 [Log] level 里改（debug/info/warn/error）
    xulei::Logger::SetLevel(
        settings.value("Log/level", "debug").toString().toStdString());
    QString gate_host = settings.value("ApiGateway/host",
                            settings.value("GateServer/host")).toString();
    QString gate_port = settings.value("ApiGateway/port",
                            settings.value("GateServer/port")).toString();
    // 拼成 "http://localhost:8080" 存进全局变量，各界面再用它拼具体接口
    gate_url_prefix = "http://"+gate_host+":"+gate_port;
    // 窗口图标：Windows 下走 .pro 的 RC_ICONS（编译进 exe 资源），
    // Linux 没有 .rc 机制，必须从 qrc 资源里设置。
    // 注意：不能用 icon.ico —— Linux 下 Qt 缺 qico 插件解不了 ico，图标会显示成空白块
    a.setWindowIcon(QIcon(":/res/icon.png"));
    MainWindow w;                        // 创建主窗口（它内部再创建登录/注册对话框）
    w.show();                            // 显示窗口
    return QCoreApplication::exec();     // 进入事件循环，程序在这里一直"挂着"直到窗口关闭
}
