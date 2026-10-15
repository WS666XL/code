#ifndef CHATUSERLIST_H
#define CHATUSERLIST_H
#include <QListWidget>
#include <QWheelEvent>
#include <QEvent>
#include <QScrollBar>
#include <QDebug>

/******************************************************************************
 *  文件：chatuserlist.h
 *  作用：聊天用户列表（在 chatdialog.ui 里已经放好了，直接用它的 ui 类型）
 *  两个自定义行为：
 *    1. 滚动条平时隐藏，鼠标移进列表才显示（像微信那样）
 *    2. 滚到最底部时发信号，让上层去加载更多聊天用户
 *****************************************************************************/
class ChatUserList: public QListWidget
{
    Q_OBJECT
public:
    ChatUserList(QWidget *parent = nullptr);
protected:
    // 事件过滤器：装在 viewport() 上，收到鼠标进入/离开/滚动事件时做处理
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void sig_loading_chat_user();  // 滚到底了，让 ChatDialog 去加载更多数据
};

#endif // CHATUSERLIST_H