#ifndef APPLYFRIENDLIST_H
#define APPLYFRIENDLIST_H
#include <QListWidget>
#include <QEvent>
// 好友申请列表
class ApplyFriendList: public QListWidget
{
    Q_OBJECT
public:
    ApplyFriendList(QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:

signals:
    void sig_show_search(bool);// 显示或隐藏搜索界面
};

#endif // APPLYFRIENDLIST_H