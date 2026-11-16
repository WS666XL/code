# ===== 文件传输独立联调 demo：验证客户端 ↔ 资源服务器(127.0.0.1:9090) 的分块上传链路 =====
# 独立 qmake 工程，不依赖 ../testqt 的任何头文件，协议在 filedialog.h 里自带

# 用到的 Qt 模块：widgets=界面控件，network=TCP 套接字
QT += widgets network

# 使用 C++17 标准，与主工程保持一致
CONFIG += c++17

# 可执行文件输出目录与目标名
DESTDIR = ./bin
TARGET = filedemo

SOURCES += \
    filedialog.cpp \
    main.cpp

HEADERS += \
    filedialog.h
