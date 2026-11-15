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

class LoadingDlg;
class UserInfoPage;   // day36：用户信息页（头像裁剪/本地保存），实现见 userinfopage.h

/******************************************************************************
 *  文件：chatdialog.h
 *  作用：聊天主界面（登录成功后 MainWindow 切到这里）
 *  布局：左侧竖排两个图标（聊天/ 联系人），中间靠左是列表，靠右是 stackedWidget
 *  数据流：登录后 loadChatList() 发 1025 拉会话列表 → 每个会话建 ChatThreadData
 *          + 聊天列表条目；接着 loadChatMsg() 发 1029 逐个会话预加载历史消息；
 *          聊天记录按 thread_id 归组缓存在 UserMgr 里
 *****************************************************************************/
class ChatDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChatDialog(QWidget *parent = nullptr);
    ~ChatDialog();

    void loadChatList();   //发 1025：按 GetLastChatThreadId 翻页拉会话列表
    void loadChatMsg();    //发 1029：拉当前预加载会话的历史消息

    void ClearLabelState(StateWidget*lb);  // 取消其他图标的选中/悬停状态，保证同一时刻只有一个是选中的

protected:
    // 事件过滤器：监听整个对话框的鼠标按下，用于"点击空白处收起搜索框"
    virtual bool eventFilter(QObject*watched ,QEvent*event)override;
    void handleGlobalMousePress(QMouseEvent * event);  // 处理全局鼠标按下：判断点的是不在搜索结果上
    void UpdateChatMsg(std::vector<std::shared_ptr<TextChatData>> msgdata);
private:
    void showLoadingDlg(bool show = true); //加载会话列表时的全屏蒙层
    void ShowSearch(bool bsearch=false);// 三种模式互相切换：搜索列表 / 聊天列表 / 联系人列表
    void AddLBGroup(StateWidget* lb); // 把侧边栏图标登记到 _lb_list，方便统一管理选中状态
    //选中列表条目/聊天页：参数是 thread_id（0 表示选第一项）
    //注意和旧版的区别：旧版按 uid 选中，新版整个聊天列表都按 thread_id 组织
    void SetSelectChatItem(int thread_id = 0);
    void RefreshChatListItem(int thread_id, std::shared_ptr<ChatThreadData> thread_data); //刷新列表条目(最后消息+时间)
    void SetSelectChatPage(int thread_id = 0);
    void loadMoreChatUser();
    void loadMoreConUser();
    Ui::ChatDialog *ui;      // 界面对象
    ChatUIMode _mode;        // 当前实际显示的是哪个列表（真正的"现在显示什么"）
    ChatUIMode _state;       // 用户点的是哪个侧边栏图标（记住意图，搜索框收起后要回到这里）
    bool _b_loading;         // 是否正在加载，防止重复触发加载动画
    QList<StateWidget*>_lb_list;  // 侧边栏所有图标控件的集合
    QWidget* _last_widget;
    //thread_id 和聊天列表条目的映射：跳转/选中某个会话时靠它找到列表项
    QMap<int, QListWidgetItem*> _chat_thread_items;
    //当前正在聊天的会话 id（旧版是 _cur_chat_uid，现在一切按 thread_id 走）
    int _cur_chat_thread_id;
    QTimer* _timer; // 心跳定时器：每 10s 发一次 ID_HEART_BEAT_REQ 保持链路活跃
    LoadingDlg* _loading_dlg;                    //全屏加载蒙层
    std::shared_ptr<ChatThreadData> _cur_load_chat;  //当前正在预加载历史消息的会话
    //day36：设置入口和用户信息页。
    //_side_settings_lb 不在 chatdialog.ui 里，构造函数里动态 new 出来
    //插到 verticalLayout_3（side_head_lb/side_chat_lb/side_contact_lb 之后），
    //不动 .ui 文件
    StateWidget* _side_settings_lb;
    UserInfoPage* _user_info_page;
private slots:
    void slot_loading_chat_user();          // 列表滚到底触发：加载更多（新版会话列表由服务端下发，这里仅占位）
    void slot_side_chat();                  // 点"聊天"图标：切到聊天页
    void slot_side_contact();               // 点"联系人"图标：切到联系人页
    void slot_side_setting();               // day36：点"设置"图标：切到用户信息页
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
    //对端发来的消息（1019 通知）：归档进会话缓存，当前会话的再追加气泡
    void slot_text_chat_msg(std::vector<std::shared_ptr<TextChatData>> msglists);
    //day39：1036 自己发的图片入库回执（触发 1037 上传）/ 1039 收到对方图片消息
    void slot_chat_img_rsp(int thread_id, std::shared_ptr<ImgChatData> chat_data);
    void slot_img_chat_msg(std::shared_ptr<ImgChatData> imgchat);
    //1026 回包：建会话数据和列表条目，没拉完就继续发 1025，拉完开始加载消息
    void slot_load_chat_thread(bool load_more, int last_thread_id,
        std::vector<std::shared_ptr<ChatThreadInfo>> chat_threads);
    //1028 回包：新建私聊会话条目并跳转过去
    void slot_create_private_chat(int uid, int other_id, int thread_id);
    //1029 回包：把历史消息装进会话缓存，翻页/下一个会话继续加载
    void slot_load_chat_msg(int thread_id, int msg_id, bool load_more,
        std::vector<std::shared_ptr<ChatDataBase>> msglists);
    //1018 自己发消息的回执：MoveMsg 补 msg_id + 刷新气泡状态图标
    void slot_add_chat_msg(int thread_id, std::vector<std::shared_ptr<TextChatData>> msglists);
};

#endif // CHATDIALOG_H
