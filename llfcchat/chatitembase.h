#ifndef CHATITEMBASE_H
#define CHATITEMBASE_H
#include<QWidget>
#include<QLabel>
#include<QGridLayout>
#include"global.h"
class BubbleFrame;

/******************************************************************************
 *  文件：chatitembase.h
 *  作用：聊天列表里"一条消息"的骨架：头像 + 用户名 + 气泡
 *  说明：气泡本身内容不确定（文字/图片/文件），所以先用占位控件，
 *        之后再用 setWidget() 把真正的气泡换进去
 *****************************************************************************/
class ChatItemBase:public QWidget//聊天项基础
{
    Q_OBJECT
public:
    //rple 是 ChatRole：Self 表示"我自己发的"，Other 表示"对方发的"，
//         构造函数里根据它决定头像和名字放左边还是右边
   explicit ChatItemBase(ChatRole rple,QWidget *parent=nullptr);
    void setUserName(const QString &name);  // 设置用户名标签
    void setUserIcon(const QPixmap &icon);  // 设置头像（会自动缩放到 42×42）
    void setWidget(QWidget* w);            // 把占位气泡替换成真正的气泡控件（TextBubble / PictureBubble）
private:
    ChatRole m_role;         // 消息归属：自己发的 / 对方发的
    QLabel *m_pNameLabel;    // 用户名标签
    QLabel *m_pIconLabel;    // 头像标签（42×42）
    QWidget* m_pBubble;      // 气泡容器占位符，setWidget() 之后指向真正的气泡

};

#endif // CHATITEMBASE_H
