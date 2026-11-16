#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>
#include <QDate>
#include"userdata.h"
#include <QMap>
#include <QHash>
#include <QPointer>
namespace Ui {
// 前向声明：由 uic 根据 chatpage.ui 自动生成
class ChatPage;
}

class ChatItemBase;
class QToolButton;

/******************************************************************************
 *  文件：chatpage.h
 *  作用：聊天页面（ChatDialog 里 stackedWidget 的第 0 页）
 *  结构：左边消息列表（ChatView）+ 右边输入区（MessageTextEdit）+ 发送按钮
 *  数据：直接持有 ChatThreadData（当前会话），
 *        切换会话时 SetChatData() 整体重画，新消息用 AppendChatMsg() 增量追加
 *        本轮只做文本消息（图片/文件发送留待后续）
 *****************************************************************************/
class ChatPage : public QWidget
{
    Q_OBJECT

signals:
    //删除了一条消息：通知 ChatDialog 刷新左侧会话条目的最后消息/时间
    void sig_chat_msg_deleted(int thread_id);

public:
    explicit ChatPage(QWidget *parent = nullptr);
    ~ChatPage();
    //切换到某个会话：清空列表后按会话缓存里的消息整体重画
    void SetChatData(std::shared_ptr<ChatThreadData> chat_data);
    //追加一条消息气泡；rsp=false 表示"还没收到服务端回执"的消息（按 unique_id 记录）
    void AppendChatMsg(std::shared_ptr<ChatDataBase> msg, bool rsp=true);
    //收到 1018 回执后：把气泡旁的状态图标从未读改成回执状态
    void UpdateChatStatus(std::shared_ptr<ChatDataBase> msg);
    //收到 1036 回包后：自己发的图片消息气泡改回执状态（随后发起 1037 上传）
    void UpdateImgChatStatus(std::shared_ptr<ImgChatData> msg);
    //头像变更实时刷新（1057）：当前会话里所有由该用户发出的消息气泡重刷头像
    void RefreshAvatarForUid(int uid);
    //好友备注变化后刷新聊天页标题（备注 > 昵称 > 用户名）
    void RefreshFriendDisplay();
    //好友备注变化后刷新当前会话里"对方发的"气泡名字
    void RefreshFriendNameForUid(int uid);
    //自己的昵称/资料变化后刷新当前会话里自己气泡的名字
    void RefreshSelfInfo();
    //删除好友后清空当前聊天页（会话缓存由 ChatDialog 移除）
    void ClearChat();
protected:
    // 重写绘图事件：背景是圆角矩形，所以自己画，不交给 QWidget 默认画
   virtual void paintEvent(QPaintEvent *event) override;

private slots:
    void on_send_btn_clicked();  // 点发送按钮：取出输入框里的消息，生成气泡并打包发给服务器
private:
    void clearItems();           // 清空消息列表和状态记录
    //给图片/文件气泡挂上断点续传上下文：记录 unique_name、上传/下载方向，
    //并把气泡的暂停/继续按钮接到 FileTcpMgr 对应接口
    void AttachTransferContext(QWidget* bubble, const QString& name, bool is_upload);
    //表情面板：点笑脸弹出/收起，点表情插入到输入框光标处
    void ToggleEmojiPanel();
    //群聊里显示发送者昵称：好友取 nick（没有则 name），非好友兜底 uid
    QString DisplayNameForUid(int uid);
    //按消息日期在聊天页插入"今天/昨天/更早日期"分隔条
    void MaybeAppendDateSeparator(const QString& chat_time);
    //聊天页标题右侧"..."菜单：好友备注 / 删除好友
    void ShowFriendMenu();
    //右键气泡弹"删除"菜单：msg 用于删缓存、发 1061 服务端标记
    void AttachDeleteMenu(QWidget* bubble, std::shared_ptr<ChatDataBase> msg);
    //删除单条消息：先从页面/本地缓存移除，再发 1061 落服务端删除标记
    void DeleteChatMsg(std::shared_ptr<ChatDataBase> msg);
    Ui::ChatPage *ui;      // 界面对象
    std::shared_ptr<ChatThreadData> _chat_data;  // 当前会话
    //管理未回复（还没收到回执）的消息气泡：key 是本地 unique_id
    QHash<QString, ChatItemBase*> _unrsp_item_map;
    //管理已回执的消息气泡：key 是服务端 msg_id（以后做图片进度更新用得上）
    QHash<int, ChatItemBase*> _base_item_map;
    //本会话里"本地缺文件、等待下载"的气泡：key 是唯一文件名，value 是下载完成后
    //的刷新回调（图片气泡换真图 / 文件卡片切"点击打开"状态）。day41 改为回调式：
    //图片气泡和文件气泡类型不同，各自捕获自己刷新方法的 lambda，chatpage 不感知类型
    QHash<QString, std::function<void()>> _download_done_map;
    //下载失败的清理回调：key 是唯一文件名（值通常是空操作或恢复失败态）
    QHash<QString, std::function<void()>> _download_fail_map;
    //上传中的气泡：key 是唯一文件名，value 是图片/文件气泡控件。
    //1038 回执驱动上传进度，按名字找回气泡刷进度条；QPointer 保证
    //切换会话/气泡销毁后不会悬空访问
    QHash<QString, QPointer<QWidget>> _upload_bubbles;
    //下载中的气泡：key 是唯一文件名，1034 下载进度按名字找回气泡刷进度/切暂停态
    QHash<QString, QPointer<QWidget>> _download_bubbles;
    //表情面板（懒创建，第一次点笑脸时构造，之后复用）
    QPointer<QWidget> _emoji_panel;
    //上一条消息的日期：用于判断是否需要插入新的日期分隔条
    QDate _last_sep_date;
    //标题右侧"..."按钮（私聊显示，群聊隐藏）
    QToolButton* _more_btn = nullptr;
};

#endif // CHATPAGE_H
