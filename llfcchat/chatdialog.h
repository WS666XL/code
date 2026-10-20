#ifndef CHATDIALOG_H
#define CHATDIALOG_H
#include"global.h"
#include <QDialog>
#include"statewidget.h"
#include<QMouseEvent>
#include"userdata.h"
#include<QListWidgetItem>
namespace Ui {
// 前向声明：由 uic 根据 chatdialog.ui 自动生成
class ChatDialog;
}

/******************************************************************************
 *  文件：chatdialog.h
 *  作用：聊天主界面（登录成功后 MainWindow 切到这里）
 *  布局：左侧竖排两个图标（聊天/ 联系人），中间靠左是列表，靠右是 stackedWidget
 *****************************************************************************/
class ChatDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChatDialog(QWidget *parent = nullptr);
    ~ChatDialog();

    void addChatUserList();     // 往列表里塞测试用的假数据（13 条聊天用户）

    void ClearLabelState(StateWidget*lb);  // 取消其他图标的选中/悬停状态，保证同一时刻只有一个是选中的

protected:
    // 事件过滤器：监听整个对话框的鼠标按下，用于"点击空白处收起搜索框"
    virtual bool eventFilter(QObject*watched ,QEvent*event)override;
    void handleGlobalMousePress(QMouseEvent * event);  // 处理全局鼠标按下：判断点的是不在搜索结果上
        void UpdateChatMsg(std::vector<std::shared_ptr<TextChatData>> msgdata);
private:
    void ShowSearch(bool bsearch=false);// 三种模式互相切换：搜索列表 / 聊天列表 / 联系人列表
    void AddLBGroup(StateWidget* lb); // 把侧边栏图标登记到 _lb_list，方便统一管理选中状态
    void SetSelectChatItem(int uid = 0);
    void SetSelectChatPage(int uid = 0);
     void loadMoreChatUser();
    void loadMoreConUser();
    Ui::ChatDialog *ui;      // 界面对象
    ChatUIMode _mode;        // 当前实际显示的是哪个列表（真正的"现在显示什么"）
    ChatUIMode _state;       // 用户点的是哪个侧边栏图标（记住意图，搜索框收起后要回到这里）
    bool _b_loading;         // 是否正在加载，防止重复触发加载动画
    QList<StateWidget*>_lb_list;  // 侧边栏所有图标控件的集合
      int _cur_chat_uid;
    QWidget* _last_widget;
    QMap<int, QListWidgetItem*> _chat_items_added;
private slots:
    void slot_loading_chat_user();          // 列表滚到底触发：先显示加载动画，1 秒后插入新数据
    void slot_side_chat();                  // 点"聊天"图标：切到聊天页
    void slot_side_contact();               // 点"联系人"图标：切到联系人页
    void slot_text_changed(const QString& str);  // 搜索框内容变化：非空就显示搜索结果列表
    void slot_loading_contact_user();

public slots:
    void slot_apply_friend(std::shared_ptr<AddFriendApply> apply);
    void slot_add_auth_friend(std::shared_ptr<AuthInfo> auth_info);
    void slot_auth_rsp(std::shared_ptr<AuthRsp> auth_rsp);
    void slot_jump_chat_item(std::shared_ptr<SearchInfo> si);
    void slot_friend_info_page(std::shared_ptr<UserInfo> user_info);
    void slot_switch_apply_friend_page();
    void slot_jump_chat_item_from_infopage(std::shared_ptr<UserInfo> ui);
    void slot_item_clicked(QListWidgetItem *item);
    void slot_append_send_chat_msg(std::shared_ptr<TextChatData> msgdata);
    void slot_text_chat_msg(std::shared_ptr<TextChatMsg> msg);
};

#endif // CHATDIALOG_H
