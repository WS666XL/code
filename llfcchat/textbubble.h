#ifndef TEXTBUBBLE_H
#define TEXTBUBBLE_H

#include <QTextEdit>
#include "BubbleFrame.h"
#include <QHBoxLayout>

/******************************************************************************
 *  文件：textbubble.h
 *  作用：文字消息的气泡（自己实现气泡基类 BubbleFrame）
 *  为什么用 QTextEdit 而不是 QLabel：
 *  QLabel 不能自动换行 + 自适应高度，QTextEdit 可以，气泡高度才能跟着内容变
 *****************************************************************************/
class TextBubble : public BubbleFrame
{
    Q_OBJECT
public:
    TextBubble(ChatRole role, const QString &text, QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *o, QEvent *e);  // 事件过滤器：处理气泡内 QTextEdit 的鼠标事件
private:
    void adjustTextHeight();      // 按文字行数调整气泡高度（让气泡紧贴文字）
    void setPlainText(const QString &text);   // 设置文字内容并去掉默认边框/只读背景
    void initStyleSheet();        // 给 QTextEdit 挂上无边框、背景透明的样式
private:
    QTextEdit *m_pTextEdit;       // 气泡里显示文字的控件
};

#endif // TEXTBUBBLE_H