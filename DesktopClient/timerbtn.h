#ifndef TIMERBTN_H
#define TIMERBTN_H
#include <QPushButton>
#include <QTimer>

// 带倒计时的按钮：点一次后自动禁用并倒数若干秒，防止重复点击（"获取验证码"用的就是它）
class TimerBtn : public QPushButton
{
public:
    TimerBtn(QWidget *parent = nullptr);   // 构造函数，parent 指定父控件
    ~ TimerBtn();                          // 析构函数

    // 重写mouseReleaseEvent
    virtual void mouseReleaseEvent(QMouseEvent *e) override;
private:
    QTimer  *_timer;    // 倒计时定时器，每秒触发一次
    int _counter;       // 剩余秒数
};

#endif // TIMERBTN_H