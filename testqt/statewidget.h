#ifndef STATEWIDGET_H
#define STATEWIDGET_H
#include <QWidget>
#include "global.h"
#include <QLabel>

/******************************************************************************
 *  文件：statewidget.h
 *  作用：带"普通 / 悬停 / 按下 / 选中"多套状态的可点击控件
 *        侧边栏的聊天图标、联系人图标就是用它做的
 *  实现思路：不用 QSS 伪类（QWidget 不支持 :hover），
 *        而是重写鼠标事件 + setProperty("state", ...) + repolish()，
 *        让 QSS 里的 [state="xx"] 选择器重新生效
 *****************************************************************************/
class StateWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StateWidget(QWidget *parent = nullptr);
    //设置状态：六个参数分别对应 QSS 里的六种 state 值
    void SetState(QString normal="", QString hover="", QString press="",
                  QString select="", QString select_hover="", QString select_press="");
    //获取当前状态
    ClickLbState GetCurState();
    void ClearState();//清除状态：把状态重置成"普通"（其他图标调它来取消选中效果）

    void SetSelected(bool bselected);//设置为已选（侧边栏切页时调用）
    void AddRedPoint();//添加红点（未读消息提示的小红点）
    void ShowRedPoint(bool show=true);//显示红点

protected:
    //绘图事件：让 QSS 里的背景、边框生效
    virtual void paintEvent(QPaintEvent* event)override;

    virtual void mousePressEvent(QMouseEvent *ev) override;//鼠标点击事件：切到"按下"样式
    virtual void mouseReleaseEvent(QMouseEvent *ev) override;//鼠标释放事件：切回"悬停"样式并发 clicked 信号
    virtual void enterEvent(QEnterEvent* event) override;//进入事件：切到"悬停"样式
    virtual void leaveEvent(QEvent* event) override;//离开事件：切回"普通"样式

private:

    QString _normal;//普通状态，对应 QSS [state="normal"]
    QString _normal_hover;//普通悬停，对应 [state="hover"]
    QString _normal_press;//普通点击，对应 [state="press"]

    QString _selected;//选中状态，对应 [state="selected_normal"]
    QString _selected_hover;//选中悬停，对应 [state="selected_hover"]
    QString _selected_press;//选中点击，对应 [state="selected_press"]

    ClickLbState _curstate;//当前状态（Normal 普通 / Selected 选中）
    QLabel * _red_point;    // 未读消息红点

signals:
    void clicked(void);      // 鼠标点击完成时发出，外部连它来做切页等操作

signals:

public slots:
};

#endif // STATEWIDGET_H