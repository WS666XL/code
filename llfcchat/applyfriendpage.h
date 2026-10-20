#ifndef APPLYFRIENDPAGE_H
#define APPLYFRIENDPAGE_H

#include <QWidget>
#include "userdata.h"
#include <memory>
#include <QJsonArray>
#include <unordered_map>
#include "applyfrienditem.h"

namespace Ui {
class ApplyFriendPage;//申请好友页
}

class ApplyFriendPage : public QWidget
{
    Q_OBJECT

public:
    explicit ApplyFriendPage(QWidget *parent = nullptr);
    ~ApplyFriendPage();
    void AddNewApply(std::shared_ptr<AddFriendApply> apply);
protected:
    void paintEvent(QPaintEvent *event);
private:
    void loadApplyList();  // 加载好友申请列表
    Ui::ApplyFriendPage *ui;
    // 保存未处理的好友申请
    // key：申请人的ID
    // value：对应的申请项控件
    std::unordered_map<int, ApplyFriendItem*> _unauth_items;
public slots:
    // 处理好友申请的回复
    void slot_auth_rsp(std::shared_ptr<AuthRsp> );
signals:
      // 显示或隐藏搜索界面
    void sig_show_search(bool);
};
#endif // APPLYFRIENDPAGE_H
