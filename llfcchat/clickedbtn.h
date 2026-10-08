#ifndef CLICKEDBTN_H
#define CLICKEDBTN_H
#include<QPushButton>

/******************************************************************************
 *  文件：clickedbtn.h
 *  作用：带"普通 / 悬停 / 按下"三套图标的按钮（"添加"按钮在用）
 *  注意：只支持普通态，不支持"选中态"（选中态是 StateWidget 干的活）
 *****************************************************************************/
class ClickedBtn:public QPushButton
{
    Q_OBJECT
public:
    ClickedBtn(QWidget*parent = nullptr);
    ~ClickedBtn();
    void SetState(QString normal,QString hover,QString press);  // 传三个 qss 里的 state 名
protected:
    virtual void enterEvent(QEnterEvent *event) override; // 鼠标进入
    virtual void leaveEvent(QEvent *event) override;// 鼠标离开
    virtual void mousePressEvent(QMouseEvent *event) override; // 鼠标按下
    virtual void mouseReleaseEvent(QMouseEvent *event) override; // 鼠标释放
private:
    QString _normal;//普通状态
    QString _hover;//悬停
    QString _press;//按下
};

#endif // CLICKEDBTN_H
