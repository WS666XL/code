#ifndef CHATVIEW_H
#define CHATVIEW_H
#include<QWidget>
#include<QVBoxLayout>
#include<QScrollArea>

/******************************************************************************
 *  文件：chatview.h
 *  作用：聊天消息列表容器（内部是一个 QScrollArea，可以上下滚动）
 *  布局细节：垂直布局末尾塞了一个高度 100000 的空白控件当"弹簧"，
 *            这样插入的气泡总是靠上排列，而滚动条会被挤到最右边
 *****************************************************************************/
class ChatView: public QWidget
{
    Q_OBJECT
public:
    ChatView(QWidget *parent = Q_NULLPTR);
    void appendChatItem(QWidget *item);                 //尾插：插到末尾（发新消息用这个）
    void prependChatItem(QWidget *item);                //头插：插到最前面（往上翻历史消息用，还没实现）
    void insertChatItem(QWidget *before, QWidget *item);//中间插：插到某个控件之前（还没实现）
protected:
    bool eventFilter(QObject *o, QEvent *e) override;  // 鼠标移进/移出滚动区，控制滚动条显隐
    void paintEvent(QPaintEvent *event) override;       // 画背景（QSS 里的圆角背景）
private slots:
    void onVScrollBarMoved(int min, int max);           // 滚动条范围变化时：新插了消息就自动滚到底
private:
    void initStyleSheet();     // 初始化样式（现在是空实现）
private:
    //QWidget *m_pCenterWidget;
    QVBoxLayout *m_pVl;         // 内部容器用的垂直布局
    QScrollArea *m_pScrollArea; // 滚动区域
    bool isAppended;            // 标记：刚刚追加过消息，滚动条要自动拉到底
};
#endif // CHATVIEW_H
