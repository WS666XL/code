# ===== qmake 工程文件：描述这个项目用什么模块、由哪些文件组成 =====

# 用到的 Qt 模块：widgets=窗口与控件，network=网络(HTTP 请求)
QT += widgets network

# 使用 C++17 标准
CONFIG += c++17

# 生成的 exe 使用的图标，以及可执行文件的输出目录
RC_ICONS = icon.ico
DESTDIR = ./bin

# 打开下面这行后，凡是使用了 Qt6 之前已废弃的 API 都会直接编译失败（默认关闭）
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# 参与编译的 .cpp 源文件
SOURCES += \
    global.cpp \
    httpmgr.cpp \
    logindialog.cpp \
    main.cpp \
    mainwindow.cpp \
    registerdialog.cpp

# 参与编译的 .h 头文件
HEADERS += \
    global.h \
    httpmgr.h \
    logindialog.h \
    mainwindow.h \
    registerdialog.h \
    singleton.h

# Qt Designer 拖出来的界面文件，编译时会自动生成 ui_xxx.h
FORMS += \
    logindialog.ui \
    mainwindow.ui \
    registerdialog.ui


# 部署规则：Linux/QNX 上执行 make install 时安装到哪个路径
# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# 资源文件(图片、qss 样式等)，代码里用 ":/路径" 的形式访问其中的文件
RESOURCES += \
    rc.qrc

# ===== 编译完成后，把工程根目录的 config.ini 拷到 exe 所在目录 =====
# 为什么需要这一步：main.cpp 是用 QCoreApplication::applicationDirPath() 去
# "exe 同目录"下找 config.ini 的，不拷过去程序启动时就读不到服务器地址。
win32:CONFIG(debug, debug | release)
{
    #指定要拷贝的文件目录为工程目录下release目录下的所有dll、lib文件，例如工程目录在D:\QT\Test
    #PWD就为D:/QT/Test，DllFile = D:/QT/Test/release/*.dll
    # $$PWD = .pro 所在目录(工程目录)，$$OUT_PWD = 构建输出目录，DESTDIR = 上面的 ./bin
    TargetConfig = $${PWD}/config.ini
    #将输入目录中的"/"替换为"\"
    TargetConfig = $$replace(TargetConfig, /, \\)
    #将输出目录中的"/"替换为"\"
    OutputDir =  $${OUT_PWD}/$${DESTDIR}
    OutputDir = $$replace(OutputDir, /, \\)
    #执行copy命令
    QMAKE_POST_LINK += copy /Y \"$$TargetConfig\" \"$$OutputDir\"
}
