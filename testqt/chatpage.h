#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>
#include"userdata.h"
#include <QMap>
#include <QHash>
namespace Ui {
// 前向声明：由 uic 根据 chatpage.ui 自动生成
class ChatPage;
}

class ChatItemBase;

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
protected:
    // 重写绘图事件：背景是圆角矩形，所以自己画，不交给 QWidget 默认画
   virtual void paintEvent(QPaintEvent *event) override;

private slots:
    void on_send_btn_clicked();  // 点发送按钮：取出输入框里的消息，生成气泡并打包发给服务器
private:
    void clearItems();           // 清空消息列表和状态记录
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
};

#endif // CHATPAGE_H
