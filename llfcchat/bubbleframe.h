#ifndef BUBBLEFRAME_H
#define BUBBLEFRAME_H
#include"global.h"
#include<QHBoxLayout>
#include<QFrame>

/******************************************************************************
 *  文件：bubbleframe.h
 *  作用：气泡的基类，只负责画"带小尖角的气泡外形"
 *  说明：具体内容（文字、图片）交给子类 TextBubble / PictureBubble 填进去
 *****************************************************************************/
class BubbleFrame : public QFrame//气泡框BubbleFrame
{
    Q_OBJECT
public:
    //role 决定气泡往左还是往右，以及尖角在左边还是右边
    BubbleFrame(ChatRole role, QWidget *parent = nullptr);
    //void setMargin(int margin);
    //inline int margin(){return margin;}
    void setWidget(QWidget *w);//如果发的是图片，这个Qwidget就是图片；文字的话就是 QTextEdit
protected:
    //重写绘图事件：这里用 QSS 里画好的气泡图片当背景，控件本身只是个容器
    void paintEvent(QPaintEvent *e);
private:
    QHBoxLayout *m_pHLayout;//水平布局：气泡里的内容控件加进来
    ChatRole m_role;        // 自己发的 / 对方发的
    int      m_margin;      // 气泡与列表左右边缘的间距
};

#endif // BUBBLEFRAME_H
