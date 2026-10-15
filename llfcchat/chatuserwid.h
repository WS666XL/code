#ifndef CHATUSERWID_H
#define CHATUSERWID_H

#include <QWidget>
#include "listitembase.h"
namespace Ui {
// 前向声明：由 uic 根据 chatuserwid.ui 自动生成
class ChatUserWid;//聊天用户ID
}

/******************************************************************************
 *  文件：chatuserwid.h
 *  作用：聊天列表里的一条（头像 + 用户名 + 最后一条消息）
 *  注意：它不是 QListWidgetItem，而是普通 QWidget，
 *        由 ChatUserList::setItemWidget() 塞进列表项里显示
 *****************************************************************************/
class ChatUserWid : public ListItemBase
{
    Q_OBJECT

public:
    explicit ChatUserWid(QWidget *parent = nullptr);
    ~ChatUserWid();

    QSize sizeHint() const override {
        return QSize(250, 70); // 返回自定义的尺寸：告诉列表这一项要多高
    }

    void SetInfo(QString name, QString head, QString msg);
    // 三个参数分别是：用户名、头像路径（":/res/head_x.jpg"）、最后一条消息内容

private:
    Ui::ChatUserWid *ui;  // 界面对象
    QString _name;        // 用户名
    QString _head;        // 头像资源路径
    QString _msg;         // 最后一条消息
};

#endif // CHATUSERWID_H