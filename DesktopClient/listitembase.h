#ifndef LISTITEMBASE_H
#define LISTITEMBASE_H
#include <QWidget>
#include "global.h"

/******************************************************************************
 *  文件：listitembase.h
 *  作用：所有"自定义列表项"的基类
 *  关键作用：给每一条记录打一个"类型标记"（聊天用户 / 联系人 / 搜索结果...），
 *        点击列表时就能靠GetItemType() 知道点到的是哪一类，从而做出不同反应
 *****************************************************************************/
class ListItemBase : public QWidget//列表项基础
{
    Q_OBJECT
public:
    explicit ListItemBase(QWidget *parent = nullptr);
    void SetItemType(ListItemType itemType);  // 构造时设置自己的类型

    ListItemType GetItemType();                // 取回类型，供点击事件分发用
protected:
    virtual void paintEvent(QPaintEvent *event) override;  // 让 QSS 里的背景/圆角样式生效
private:
    ListItemType _itemType;    // 这一项的类型标记

public slots:

signals:


};

#endif // LISTITEMBASE_H