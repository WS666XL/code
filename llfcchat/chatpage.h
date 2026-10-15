#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>

namespace Ui {
// 前向声明：由 uic 根据 chatpage.ui 自动生成
class ChatPage;
}

/******************************************************************************
 *  文件：chatpage.h
 *  作用：聊天页面（ChatDialog 里 stackedWidget 的第 0 页）
 *  结构：左边消息列表（ChatView）+ 右边输入区（MessageTextEdit）+ 发送按钮
 *****************************************************************************/
class ChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPage(QWidget *parent = nullptr);
    ~ChatPage();
protected:
    // 重写绘图事件：背景是圆角矩形，所以自己画，不交给 QWidget 默认画
   virtual void paintEvent(QPaintEvent *event) override;

private slots:
    void on_send_btn_clicked();  // 点发送按钮：取出输入框里的消息，生成对应的聊天气泡加到列表

private:
    Ui::ChatPage *ui;      // 界面对象
};

#endif // CHATPAGE_H
