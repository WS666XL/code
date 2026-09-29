#ifndef CLICKEDLABEL_H
#define CLICKEDLABEL_H
#include<QLabel>
#include"global.h"
// 可点击的 QLabel：带"普通 / 悬停 / 按下 / 选中"多套样式，用作密码框旁边那个"眼睛"图标
class ClickedLabel : public QLabel
{
    Q_OBJECT
public:
    ClickedLabel(QWidget *parent);                          // 构造函数，parent 指定父控件
    virtual void enterEvent(QEnterEvent *event)override;  // 鼠标进入控件区域
    virtual void leaveEvent(QEvent *event) override;       // 鼠标离开控件区域
    virtual void mousePressEvent(QMouseEvent *e) override;   // 鼠标按下
    void SetState(QString normal="", QString hover="", QString press="",
                  QString select="", QString select_hover="", QString select_press="");
                                                            // 设置六种状态各自对应的图片/样式名
    ClickLbState GetCurState();                             // 取当前状态（Normal 普通 / Selected 选中）

private:
    QString _normal;            // 普通状态下的图片
    QString _normal_hover;      // 普通状态下鼠标悬停的图片
    QString _normal_press;      // 普通状态下鼠标按下的图片

    QString _selected;          // 选中状态下的图片
    QString _selected_hover;    // 选中状态下鼠标悬停的图片
    QString _selected_press;    // 选中状态下鼠标按下的图片

    ClickLbState _curstate;     // 当前所处状态
signals:
    void clicked(void);         // 自定义信号：控件被点击时发出
};

#endif // CLICKEDLABEL_H
