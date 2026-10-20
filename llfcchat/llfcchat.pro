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
    adduseritem.cpp \
    applyfriend.cpp \
    applyfrienditem.cpp \
    applyfriendlist.cpp \
    applyfriendpage.cpp \
    authenfriend.cpp \
    bubbleframe.cpp \
    chatdialog.cpp \
    chatitembase.cpp \
    chatpage.cpp \
    chatuserlist.cpp \
    chatuserwid.cpp \
    chatview.cpp \
    clickedbtn.cpp \
    clickedlabel.cpp \
    clickedoncelabel.cpp \
    contactuserlist.cpp \
    conuseritem.cpp \
    customizeedit.cpp \
    findfaildlg.cpp \
    findsuccessdlg.cpp \
    friendinfopage.cpp \
    friendlabel.cpp \
    global.cpp \
    grouptipitem.cpp \
    httpmgr.cpp \
    listitembase.cpp \
    loadingdlg.cpp \
    logindialog.cpp \
    main.cpp \
    mainwindow.cpp \
    messagetextedit.cpp \
    picturebubble.cpp \
    registerdialog.cpp \
    resetdialog.cpp \
    searchlist.cpp \
    statewidget.cpp \
    tcpmgr.cpp \
    textbubble.cpp \
    timerbtn.cpp \
    userdata.cpp \
    usermgr.cpp

# 参与编译的 .h 头文件
HEADERS += \
    adduseritem.h \
    applyfriend.h \
    applyfrienditem.h \
    applyfriendlist.h \
    applyfriendpage.h \
    authenfriend.h \
    bubbleframe.h \
    chatdialog.h \
    chatitembase.h \
    chatpage.h \
    chatuserlist.h \
    chatuserwid.h \
    chatview.h \
    clickedbtn.h \
    clickedlabel.h \
    clickedoncelabel.h \
    contactuserlist.h \
    conuseritem.h \
    customizeedit.h \
    findfaildlg.h \
    findsuccessdlg.h \
    friendinfopage.h \
    friendlabel.h \
    global.h \
    grouptipitem.h \
    httpmgr.h \
    listitembase.h \
    loadingdlg.h \
    logindialog.h \
    mainwindow.h \
    messagetextedit.h \
    picturebubble.h \
    registerdialog.h \
    resetdialog.h \
    searchlist.h \
    singleton.h \
    statewidget.h \
    tcpmgr.h \
    textbubble.h \
    timerbtn.h \
    userdata.h \
    usermgr.h

# Qt Designer 拖出来的界面文件，编译时会自动生成 ui_xxx.h
FORMS += \
    adduseritem.ui \
    applyfriend.ui \
    applyfrienditem.ui \
    applyfriendpage.ui \
    authenfriend.ui \
    chatdialog.ui \
    chatpage.ui \
    chatuserwid.ui \
    conuseritem.ui \
    findfaildlg.ui \
    findsuccessdlg.ui \
    friendinfopage.ui \
    friendlabel.ui \
    grouptipitem.ui \
    loadingdlg.ui \
    logindialog.ui \
    mainwindow.ui \
    registerdialog.ui \
    resetdialog.ui


# 部署规则：Linux/QNX 上执行 make install 时安装到哪个路径
# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# 资源文件(图片、qss 样式等)，代码里用 ":/路径" 的形式访问其中的文件
RESOURCES += \
    rc.qrc

    CONFIG(debug, debug | release) {
        #指定要拷贝的文件目录为工程目录下release目录下的所有dll、lib文件，例如工程目录在D:\QT\Test
        #PWD就为D:/QT/Test，DllFile = D:/QT/Test/release/*.dll
        TargetConfig = $${PWD}/config.ini
        #将输入目录中的"/"替换为"\"
        TargetConfig = $$replace(TargetConfig, /, \\)
        #将输出目录中的"/"替换为"\"
        OutputDir =  $${OUT_PWD}/$${DESTDIR}
        OutputDir = $$replace(OutputDir, /, \\)
        //执行copy命令
        QMAKE_POST_LINK += copy /Y \"$$TargetConfig\" \"$$OutputDir\" &

        # 首先，定义static文件夹的路径
        StaticDir = $${PWD}/static
        # 将路径中的"/"替换为"\"
        StaticDir = $$replace(StaticDir, /, \\)
        #message($${StaticDir})
        # 使用xcopy命令拷贝文件夹，/E表示拷贝子目录及其内容，包括空目录。/I表示如果目标不存在则创建目录。/Y表示覆盖现有文件而不提示。
        QMAKE_POST_LINK += xcopy /Y /E /I \"$$StaticDir\" \"$$OutputDir\\static\\\"

    }else{
          #release
        message("release mode")
        #指定要拷贝的文件目录为工程目录下release目录下的所有dll、lib文件，例如工程目录在D:\QT\Test
        #PWD就为D:/QT/Test，DllFile = D:/QT/Test/release/*.dll
        TargetConfig = $${PWD}/config.ini
        #将输入目录中的"/"替换为"\"
        TargetConfig = $$replace(TargetConfig, /, \\)
        #将输出目录中的"/"替换为"\"
        OutputDir =  $${OUT_PWD}/$${DESTDIR}
        OutputDir = $$replace(OutputDir, /, \\)
        //执行copy命令
        QMAKE_POST_LINK += copy /Y \"$$TargetConfig\" \"$$OutputDir\"

        # 首先，定义static文件夹的路径
        StaticDir = $${PWD}/static
        # 将路径中的"/"替换为"\"
        StaticDir = $$replace(StaticDir, /, \\)
        #message($${StaticDir})
        # 使用xcopy命令拷贝文件夹，/E表示拷贝子目录及其内容，包括空目录。/I表示如果目标不存在则创建目录。/Y表示覆盖现有文件而不提示。
         QMAKE_POST_LINK += xcopy /Y /E /I \"$$StaticDir\" \"$$OutputDir\\static\\\"
    }

    win32-msvc*:QMAKE_CXXFLAGS += /wd"4819" /utf-8

# win32:CONFIG(debug, debug | release)
# {
#     #指定要拷贝的文件目录为工程目录下release目录下的所有dll、lib文件，例如工程目录在D:\QT\Test
#     #PWD就为D:/QT/Test，DllFile = D:/QT/Test/release/*.dll
#     # $$PWD = .pro 所在目录(工程目录)，$$OUT_PWD = 构建输出目录，DESTDIR = 上面的 ./bin
#     TargetConfig = $${PWD}/config.ini
#     #将输入目录中的"/"替换为"\"
#     TargetConfig = $$replace(TargetConfig, /, \\)
#     #将输出目录中的"/"替换为"\"
#     OutputDir =  $${OUT_PWD}/$${DESTDIR}
#     OutputDir = $$replace(OutputDir, /, \\)
#     #执行copy命令
#     QMAKE_POST_LINK += copy /Y \"$$TargetConfig\" \"$$OutputDir\"
# }

DISTFILES += \
    config.ini
