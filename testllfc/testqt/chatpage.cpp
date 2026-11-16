#include "chatpage.h"
#include "ui_chatpage.h"
#include<QStyleOption>
#include<QPainter>
#include<QTextEdit>
#include<QFileInfo>
#include<QStandardPaths>
#include<QMessageBox>
#include"chatitembase.h"
#include"textbubble.h"
#include"picturebubble.h"
#include"filebubble.h"
#include"filetcpmgr.h"
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

    //下载完成：执行该文件名对应的刷新回调（图片气泡换真图 / 文件卡片切"点击打开"）
    connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_file_downloaded,
        this, [this](const QString& name) {
            auto iter = _download_done_map.find(name);
            if (iter == _download_done_map.end()) {
                return;
            }
            iter.value()();
            _download_done_map.erase(iter);
            _download_fail_map.remove(name);
        });

    //下载失败：执行失败回调（文件卡片恢复"点击下载"可重试；图片气泡保持占位）
    connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_download_failed,
        this, [this](const QString& name) {
            auto iter = _download_fail_map.find(name);
            if (iter == _download_fail_map.end()) {
                return;
            }
            iter.value()();
            _download_fail_map.erase(iter);
            _download_done_map.remove(name);
        });
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

    // day41：群会话标题显示"群名(成员数)"；私聊显示对方昵称/uid
    if (_chat_data->IsGroup()) {
        ui->title_lb->setText(QString("%1(%2)").arg(_chat_data->GetGroupName())
                              .arg(_chat_data->GetMemberCount()));
    }
    else {
        auto other_id = _chat_data->GetOtherId();
        auto friend_info = UserMgr::GetInstance()->GetFriendById(other_id);
        //对方资料查不到：标题显示 uid 兜底，消息照常画
        if (friend_info == nullptr) {
            ui->title_lb->setText(QString::number(other_id));
        }
        else {
            ui->title_lb->setText(friend_info->_name);
        }
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

    // ===== day39 图片 / day41 文件消息气泡（共用 1035-1039 链路）=====
    // 自己发的：MsgInfo.content 是本地路径（本地必有，直接显示）
    // 对方发的 / 历史记录里的：查本地缓存（图片 chatimg / 文件 chatfile），
    // 没有就显示占位（图片灰块 / 文件卡片"点击下载"）+ 后台 1033 下载，
    // 下载完成按 _download_done_map 里的回调刷新对应气泡
    if (msg->GetMsgType() == ChatMsgType::PIC || msg->GetMsgType() == ChatMsgType::FILE) {
        auto img_data = std::dynamic_pointer_cast<ImgChatData>(msg);
        if (!img_data || !img_data->_msg_info) {
            return;
        }
        auto msg_info = img_data->_msg_info;
        auto sender_uid = msg->GetSendUid();
        bool is_self = (sender_uid == self_info->_uid);
        bool is_file = (msg->GetMsgType() == ChatMsgType::FILE);
        ChatRole role = is_self ? ChatRole::Self : ChatRole::Other;

        //解析本地路径：发送方是原始路径；接收方查缓存目录（图片/文件各一个）
        QString cache_subdir = is_file ? "chatfile" : "chatimg";
        QString local_path = msg_info->content;
        if (local_path.isEmpty() || !QFileInfo::exists(local_path)) {
            QString storage_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            local_path = storage_dir + "/" + QString::number(self_info->_uid)
                         + "/" + cache_subdir + "/" + QString::number(sender_uid)
                         + "/" + msg_info->_unique_name;
            if (QFileInfo::exists(local_path)) {
                //缓存命中：记进 MsgInfo，下次重画直接走本地路径
                msg_info->content = local_path;
            }
        }

        bool need_download = false;
        ChatItemBase* pChatItem = new ChatItemBase(role);
        if (is_self) {
            pChatItem->setUserName(self_info->_name);
            pChatItem->setUserIcon(LoadAvatarPixmap(self_info->_icon, QSize(42,42), self_info->_uid));
        }
        else {
            auto friend_info = UserMgr::GetInstance()->GetFriendById(sender_uid);
            if (friend_info) {
                pChatItem->setUserName(friend_info->_name);
                pChatItem->setUserIcon(LoadAvatarPixmap(friend_info->_icon, QSize(42,42), sender_uid));
            }
            else {
                //对方资料不在好友表（异常数据）：uid 兜底 + 默认头像
                pChatItem->setUserName(QString::number(sender_uid));
                pChatItem->setUserIcon(LoadAvatarPixmap(":/res/head_1.jpg", QSize(42,42)));
            }
        }

        if (is_file) {
            //文件卡片：显示原始文件名+大小；本地有文件时点击直接打开
            auto* file_bubble = new FileBubble(
                msg_info->_file_name.isEmpty() ? msg_info->_unique_name : msg_info->_file_name,
                msg_info->_total_size, role);
            if (!local_path.isEmpty() && QFileInfo::exists(local_path)) {
                file_bubble->setLocalPath(local_path);
            }
            else {
                need_download = true;
                //点击 → 发起 1033 下载（chatfile 目录）+ 登记成功/失败回调
                connect(file_bubble, &FileBubble::sig_card_clicked, this,
                    [this, file_bubble, msg_info, sender_uid]() {
                        _download_done_map[msg_info->_unique_name] = [file_bubble, msg_info, sender_uid]() {
                            QString storage_dir = QStandardPaths::writableLocation(
                                QStandardPaths::AppDataLocation);
                            QString local_path = storage_dir + "/"
                                + QString::number(UserMgr::GetInstance()->GetUid())
                                + "/chatfile/" + QString::number(sender_uid)
                                + "/" + msg_info->_unique_name;
                            if (QFileInfo::exists(local_path)) {
                                file_bubble->setLocalPath(local_path);
                            }
                            else {
                                file_bubble->setDownloadFailed();
                            }
                        };
                        _download_fail_map[msg_info->_unique_name] = [file_bubble]() {
                            file_bubble->setDownloadFailed();
                        };
                        file_bubble->setDownloading();
                        FileTcpMgr::GetInstance()->DownloadChatFile(
                            msg_info->_unique_name, sender_uid);
                    });
            }
            pChatItem->setWidget(file_bubble);
        }
        else {
            //图片气泡：本地有图直接显示；没有则灰色占位 + 后台下载
            QPixmap pic;
            if (!local_path.isEmpty() && QFileInfo::exists(local_path)) {
                pic = QPixmap(local_path);
            }
            else {
                pic = QPixmap(160, 90);
                pic.fill(QColor(235, 235, 235));
                need_download = true;
            }
            auto* pic_bubble = new PictureBubble(pic, role);
            pChatItem->setWidget(pic_bubble);
            if (need_download) {
                _download_done_map[msg_info->_unique_name] = [pic_bubble, msg_info, sender_uid]() {
                    QString storage_dir = QStandardPaths::writableLocation(
                        QStandardPaths::AppDataLocation);
                    QString local_path = storage_dir + "/"
                        + QString::number(UserMgr::GetInstance()->GetUid())
                        + "/chatimg/" + QString::number(sender_uid)
                        + "/" + msg_info->_unique_name;
                    if (QFileInfo::exists(local_path)) {
                        pic_bubble->setPicture(QPixmap(local_path));
                    }
                };
                FileTcpMgr::GetInstance()->DownloadChatImage(
                    msg_info->_unique_name, sender_uid);
            }
        }

        pChatItem->setStatus(msg->GetStatus());
        ui->chat_data_list->appendChatItem(pChatItem);

        //本地已就绪的（发送方 / 缓存命中）：按回执状态挂气泡索引
        if (!need_download) {
            if (rsp) {
                _base_item_map[msg->GetMsgId()] = pChatItem;
            }
            else {
                _unrsp_item_map[msg->GetUniqueId()] = pChatItem;
            }
        }
        return;
    }

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

//收到 1036 回包：自己发的图片消息已入库。只改气泡状态图标；
//1037 文件上传由 chatdialog 的信号槽发起（不依赖当前会话是否在前台），
//避免用户切走会话后图片永远传不出去
void ChatPage::UpdateImgChatStatus(std::shared_ptr<ImgChatData> msg)
{
    auto iter = _unrsp_item_map.find(msg->GetUniqueId());
    //页面切换过则气泡不在未回执表里（数据层已归档），状态图标随下次重画更新
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

    // day41 群聊：touid 无意义填 0（服务端按 thread_id + form_type 走群分支）；
    // 私聊仍是对方 uid。请求 JSON 统一带 form_type 让服务端区分
    bool is_group = _chat_data->IsGroup();
    auto to_uid = is_group ? 0 : _chat_data->GetOtherId();

    const QVector<MsgInfo>& msgList = pTextEdit->getMsgList();
    QJsonObject textObj;
    QJsonArray textArray;
    int txt_size = 0;
    //请求体里的 thread_id/touid 都从会话数据里取，保证和当前会话一致
    auto thread_id = _chat_data->GetThreadId();
    for (int i=0; i<msgList.size(); ++i)
    {
        //消息内容长度不合规就跳过
        QString type = msgList[i].msgFlag;
        //文本消息超长跳过（图片的 content 是本地路径，不受此限制）
        if(type == "text" && msgList[i].content.length() > 1024){
            continue;
        }

        // day41 基础版：群聊暂不支持图片/文件（1035 的群模式适配留待后续轮次）
        if(is_group && (type == "image" || type == "file"))
        {
            QMessageBox::information(this, QStringLiteral("提示"),
                QStringLiteral("群聊暂不支持发送图片/文件"));
            continue;
        }

        // ===== day39 图片 / day41 文件消息分支（共用 1035-1039 链路）=====
        // 时序（老师版设计）：先发 1035 让服务端入库拿 message_id，
        // 气泡立即显示本地内容；收到 1036 回包后才发起 1037 文件上传，
        // 文件全部落盘后由资源服务器通知对方
        if(type == "image" || type == "file")
        {
            bool is_file = (type == "file");

            //先把之前攒着的文本发出去（文本与图片/文件交替时保证顺序）
            if (txt_size) {
                textObj["text_array"] = textArray;
                textObj["fromuid"] = user_info->_uid;
                textObj["touid"] = to_uid;
                textObj["form_type"] = is_group ? 1 : 0;
                textObj["thread_id"] = thread_id;
                QJsonDocument doc(textObj);
                QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
                txt_size = 0;
                textArray = QJsonArray();
                textObj = QJsonObject();
                emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, jsonData);
            }

            auto msg_info = std::make_shared<MsgInfo>(msgList[i]);
            auto uuidString = QUuid::createUuid().toString();

            //气泡立即显示本地内容：图片=缩略图 / 文件=文件卡片
            ChatItemBase *pChatItem = new ChatItemBase(role);
            pChatItem->setUserName(userName);
            pChatItem->setUserIcon(LoadAvatarPixmap(user_info->_icon, QSize(42,42), user_info->_uid));
            if (is_file) {
                pChatItem->setWidget(new FileBubble(msg_info->_file_name,
                    msg_info->_total_size, role));
            }
            else {
                pChatItem->setWidget(new PictureBubble(msgList[i].pixmap, role));
            }
            pChatItem->setStatus(0);
            ui->chat_data_list->appendChatItem(pChatItem);

            //挂到"未回执"表：1036 回包按 unique_id 找到它改状态；
            //消息数据同时进会话缓存（未回执区），等回包 MoveMsg 归档
            auto msg_type = is_file ? ChatMsgType::FILE : ChatMsgType::PIC;
            auto img_msg = std::make_shared<ImgChatData>(msg_info, uuidString, thread_id,
                ChatFormType::PRIVATE, msg_type, user_info->_uid, 0);
            _chat_data->AppendUnRspMsg(uuidString, img_msg);
            _unrsp_item_map[uuidString] = pChatItem;
            //登记传输信息：1036 回包按 unique_name 取回 MsgInfo 发起 1037
            UserMgr::GetInstance()->AddTransFile(msg_info->_unique_name, msg_info);

            //1035 请求（与图片消息同构，多带 msg_type/file_name 两个区分字段）
            QJsonObject imgObj;
            imgObj["fromuid"] = user_info->_uid;
            imgObj["touid"] = to_uid;
            imgObj["thread_id"] = thread_id;
            imgObj["md5"] = msg_info->_md5;
            imgObj["name"] = msg_info->_unique_name;
            imgObj["token"] = UserMgr::GetInstance()->GetToken();
            imgObj["unique_id"] = uuidString;
            imgObj["msg_type"] = static_cast<int>(msg_type);
            imgObj["file_name"] = msg_info->_file_name;
            QJsonDocument doc(imgObj);
            emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_IMG_CHAT_MSG_REQ,
                doc.toJson(QJsonDocument::Compact));
            continue;
        }

        //文件类型本轮不做，直接跳过（放在最前面，避免创建多余的气泡控件）
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
            textObj["touid"] = to_uid;
            textObj["form_type"] = is_group ? 1 : 0;
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
        textObj["touid"] = to_uid;
        textObj["form_type"] = is_group ? 1 : 0;
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

//清空消息列表和气泡索引表（切换会话时用）：
//_download_done_map/_download_fail_map 里是旧会话气泡的刷新回调，清掉防止悬空调用
void ChatPage::clearItems()
{
    ui->chat_data_list->removeAllItem();
    _unrsp_item_map.clear();
    _base_item_map.clear();
    _download_done_map.clear();
    _download_fail_map.clear();
}
