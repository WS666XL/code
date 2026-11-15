#include "chatpage.h"
#include "ui_chatpage.h"
#include<QStyleOption>
#include<QPainter>
#include<QTextEdit>
#include"chatitembase.h"
#include"textbubble.h"
#include"usermgr.h"
#include"tcpmgr.h"
#include<QJsonDocument>
#include<QJsonArray>
#include<QUuid>

ChatPage::ChatPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ChatPage)
{
    ui->setupUi(this);
    //清掉 .ui 里的占位标题"人机"：没选中会话时标题应为空
    ui->title_lb->setText("");
    //设置按钮样式
    ui->send_btn->SetState("normal","hover","press");

    //设置图标样式
    ui->emo_lb->SetState("normal","hover","press","normal","hover","press");
    ui->file_lb->SetState("normal","hover","press","normal","hover","press");
}

ChatPage::~ChatPage()
{
    delete ui;
}

//切换会话：先整体清空再按缓存重画。
//已回执的消息按 msg_id 顺序画；未回执的（本地刚发的）补在后面
void ChatPage::SetChatData(std::shared_ptr<ChatThreadData> chat_data) {
    _chat_data = chat_data;
    if (!_chat_data) {
        return;
    }

    auto other_id = _chat_data->GetOtherId();
    auto friend_info = UserMgr::GetInstance()->GetFriendById(other_id);
    //对方资料查不到：标题显示 uid 兜底，消息照常画
    if (friend_info == nullptr) {
        ui->title_lb->setText(QString::number(other_id));
    }
    else {
        ui->title_lb->setText(friend_info->_name);
    }

    clearItems();
    for(auto & msg : _chat_data->GetMsgMapRef()){
        AppendChatMsg(msg);
    }

    //未回执的消息也要画出来（rsp=false：按 unique_id 挂到未回执表，等 1018 回来改状态）
    for (auto& msg : _chat_data->GetMsgUnRspRef()) {
        AppendChatMsg(msg,false);
    }
}

//画一条消息气泡：自己发的靠右（带状态图标），对方的靠左
void ChatPage::AppendChatMsg(std::shared_ptr<ChatDataBase> msg, bool rsp)
{
    auto self_info = UserMgr::GetInstance()->GetUserInfo();
    ChatRole role;
    if (msg->GetSendUid() == self_info->_uid) {
        role = ChatRole::Self;
        ChatItemBase* pChatItem = new ChatItemBase(role);

        pChatItem->setUserName(self_info->_name);
        //头像走全局缓存加载（避免重复解码）；气泡 item 不参与下载回显的自动刷新，
        //本地缺文件时后台下载，重开会话时自然显示
        pChatItem->setUserIcon(LoadAvatarPixmap(self_info->_icon, QSize(42,42), self_info->_uid));
        QWidget* pBubble = nullptr;
        //本轮只画文本气泡；图片等类型后续再加分支
        if (msg->GetMsgType() == ChatMsgType::TEXT) {
            pBubble = new TextBubble(role, msg->GetMsgContent());
        }

        pChatItem->setWidget(pBubble);
        auto status = msg->GetStatus();
        pChatItem->setStatus(status);
        ui->chat_data_list->appendChatItem(pChatItem);
        if (rsp) {
            //已回执：按服务端 msg_id 记录
            _base_item_map[msg->GetMsgId()] = pChatItem;
        }
        else {
            //未回执：按本地 unique_id 记录，等 1018 回来配对
            _unrsp_item_map[msg->GetUniqueId()] = pChatItem;
        }
    }
    else {
        role = ChatRole::Other;
        ChatItemBase* pChatItem = new ChatItemBase(role);
        auto friend_info = UserMgr::GetInstance()->GetFriendById(msg->GetSendUid());
        if (friend_info == nullptr) {
            return;
        }
        pChatItem->setUserName(friend_info->_name);
        pChatItem->setUserIcon(LoadAvatarPixmap(friend_info->_icon, QSize(42,42), friend_info->_uid));
        QWidget* pBubble = nullptr;
        if (msg->GetMsgType() == ChatMsgType::TEXT) {
            pBubble = new TextBubble(role, msg->GetMsgContent());
        }
        pChatItem->setWidget(pBubble);
        auto status = msg->GetStatus();
        pChatItem->setStatus(status);
        ui->chat_data_list->appendChatItem(pChatItem);
        if (rsp) {
            _base_item_map[msg->GetMsgId()] = pChatItem;
        }
        else {
            _unrsp_item_map[msg->GetUniqueId()] = pChatItem;
        }
    }
}

//收到 1018 回执：按 unique_id 找到"未回执"的气泡，刷新状态图标，
//并把它挪进"已回执"表（此后靠 msg_id 索引）
void ChatPage::UpdateChatStatus(std::shared_ptr<ChatDataBase> msg)
{
    auto iter = _unrsp_item_map.find(msg->GetUniqueId());
    //没找到则直接返回（可能页面刚切换过，气泡已被重建到 _base_item_map 里）
    if (iter == _unrsp_item_map.end()) {
        return;
    }

    iter.value()->setStatus(msg->GetStatus());
    _base_item_map[msg->GetMsgId()] = iter.value();
    _unrsp_item_map.erase(iter);
}

void ChatPage::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ChatPage::on_send_btn_clicked()
{
    if (_chat_data == nullptr) {
        qDebug() << "friend_info is empty";
        return;
    }

    auto user_info = UserMgr::GetInstance()->GetUserInfo();
    auto pTextEdit = ui->chatEdit;
    ChatRole role = ChatRole::Self;
    QString userName = user_info->_name;

    const QVector<MsgInfo>& msgList = pTextEdit->getMsgList();
    QJsonObject textObj;
    QJsonArray textArray;
    int txt_size = 0;
    //请求体里的 thread_id/touid 都从会话数据里取，保证和当前会话一致
    auto thread_id = _chat_data->GetThreadId();
    for (int i=0; i<msgList.size(); ++i)
    {
        //消息内容长度不合规就跳过
        if(msgList[i].content.length() > 1024){
            continue;
        }

        QString type = msgList[i].msgFlag;
        //图片/文件类型本轮不做，直接跳过（放在最前面，避免创建多余的气泡控件）
        if(type != "text")
        {
            continue;
        }

        ChatItemBase *pChatItem = new ChatItemBase(role);
        pChatItem->setUserName(userName);
        pChatItem->setUserIcon(LoadAvatarPixmap(user_info->_icon, QSize(42,42), user_info->_uid));
        QWidget *pBubble = nullptr;

        //生成唯一id：本地标识这条消息，1018 回执按它配对
        QUuid uuid = QUuid::createUuid();
        //转为字符串
        QString uuidString = uuid.toString();

        pBubble = new TextBubble(role, msgList[i].content);
        //单包超过 1024 字节就先把之前攒的文本发出去，防止包过大
        if(txt_size + msgList[i].content.length()> 1024){
            textObj["fromuid"] = user_info->_uid;
            textObj["touid"] = _chat_data->GetOtherId();
            textObj["thread_id"] = thread_id;
            textObj["text_array"] = textArray;
            QJsonDocument doc(textObj);
            QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
            //发送并清空之前累计的文本列表
            txt_size = 0;
            textArray = QJsonArray();
            textObj = QJsonObject();
            //发送tcp请求给chat server
            emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, jsonData);
        }

        txt_size += msgList[i].content.length();
        QJsonObject obj;
        QByteArray utf8Message = msgList[i].content.toUtf8();
        auto content = QString::fromUtf8(utf8Message);
        //新协议字段名：msgid 改成 unique_id，并且请求体必须带 thread_id
        obj["content"] = content;
        obj["unique_id"] = uuidString;
        textArray.append(obj);
        //先按私聊组织消息数据，状态 0 = 未读
        auto txt_msg = std::make_shared<TextChatData>(uuidString, thread_id, ChatFormType::PRIVATE,
            ChatMsgType::TEXT, content, user_info->_uid, 0);
        //将未回复的消息加入到会话缓存，等 1018 回执后 MoveMsg 补上 msg_id
        _chat_data->AppendUnRspMsg(uuidString, txt_msg);

        //发送消息：先画气泡再发请求，气泡按 unique_id 挂到未回执表
        if(pBubble != nullptr)
        {
            pChatItem->setWidget(pBubble);
            pChatItem->setStatus(0);
            ui->chat_data_list->appendChatItem(pChatItem);
            _unrsp_item_map[uuidString] = pChatItem;
        }

    }

    //把剩余攒着的文本发出去（txt_size>0 说明这一轮至少有一条文本）
    if (txt_size > 0) {
        qDebug() << "textArray is " << textArray;
        //发送给服务器
        textObj["text_array"] = textArray;
        textObj["fromuid"] = user_info->_uid;
        textObj["touid"] = _chat_data->GetOtherId();
        textObj["thread_id"] = thread_id;
        QJsonDocument doc(textObj);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
        //发送并清空之前累计的文本列表
        txt_size = 0;
        textArray = QJsonArray();
        textObj = QJsonObject();
        //发送tcp请求给chat server
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, jsonData);
    }
}

//清空消息列表和两张气泡索引表（切换会话时用）
void ChatPage::clearItems()
{
    ui->chat_data_list->removeAllItem();
    _unrsp_item_map.clear();
    _base_item_map.clear();
}
