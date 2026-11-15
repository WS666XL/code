#include <QApplication>
#include "filedialog.h"

// 文件传输联调 demo 入口：只弹一个 FileDialog 对话框，
// 用来对着 ResourceServer(127.0.0.1:9090) 验证分块上传协议
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    FileDialog w;
    w.show();

    return app.exec();
}
