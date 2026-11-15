#ifndef ADDUSERITEM_H
#define ADDUSERITEM_H

#include <QWidget>
#include"listitembase.h"
namespace Ui {
// 前向声明：由 uic 根据 adduseritem.ui 自动生成
class AddUserItem;
}

/******************************************************************************
 *  文件：adduseritem.h
 *  作用：联系人列表里的"添加好友"提示条目（灰色不可点的那种）
 *****************************************************************************/
class AddUserItem : public ListItemBase
{
    Q_OBJECT

public:
    explicit AddUserItem(QWidget *parent = nullptr);
    ~AddUserItem();
    virtual QSize sizeHint() const override {
        return QSize(250, 70); // 返回自定义的尺寸：告诉列表这一项要多高
    }
protected:

private:
    Ui::AddUserItem *ui;  // 界面对象
};

#endif // ADDUSERITEM_H
