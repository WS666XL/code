#include "chatpage.h"
#include "ui_chatpage.h"
#include<QStyleOption>
#include<QPainter>
#include<QTextEdit>
#include<QFileInfo>
#include<QStandardPaths>
#include<QMessageBox>
#include"chatitembase.h"
#include"bubbleframe.h"
#include"textbubble.h"
#include"picturebubble.h"
#include"filebubble.h"
#include"filetcpmgr.h"
#include"usermgr.h"
#include"tcpmgr.h"
#include<QJsonDocument>
#include<QJsonArray>
#include<QUuid>
#include<QFileDialog>
#include<QFrame>
#include<QFont>
#include<QGridLayout>
#include<QHBoxLayout>
#include<QToolButton>
#include<QTimer>
#include<QScreen>
#include<QGuiApplication>
#include<QDateTime>
#include<QLabel>
#include<QMenu>
#include<QAction>
#include<QCursor>

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

    // 回形针按钮：WSL 下拖拽文件不方便，点它直接弹系统文件选择框。
    // 选中后走 MessageTextEdit::insertFileFromUrl，与拖拽/粘贴完全同一条插入路径：
    // 图片显示预览按图片消息发送，其他文件显示文件卡片按文件消息发送
    ui->file_lb->setCursor(Qt::PointingHandCursor);
    ui->file_lb->setToolTip(tr("选择文件"));
    connect(ui->file_lb, &ClickedLabel::clicked, this,
        [this](const QString&, ClickLbState) {
            QStringList files = QFileDialog::getOpenFileNames(
                this, tr("选择文件"), QString(), tr("所有文件 (*)"));
            if (!files.isEmpty()) {
                ui->chatEdit->insertFileFromUrl(files);
            }
        });

    // 笑脸按钮：弹出 Unicode 表情面板，点表情插入输入框光标处。
    // 消息仍然走普通文本链路，不改协议（DB 的 content 是 utf8mb4，支持 4 字节 emoji）
    ui->emo_lb->setCursor(Qt::PointingHandCursor);
    ui->emo_lb->setToolTip(tr("表情"));
    connect(ui->emo_lb, &ClickedLabel::clicked, this,
        [this](const QString&, ClickLbState) {
            ToggleEmojiPanel();
        });

    //下载完成：执行该文件名对应的刷新回调（图片气泡换真图 / 文件卡片切"点击打开"）
    connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_file_downloaded,
        this, [this](const QString& name) {
            //先让气泡切到完成态（进度条到 100% 再收起），再执行刷新回调
            if (auto it = _download_bubbles.find(name); it != _download_bubbles.end()) {
                if (QWidget* bubble = it.value()) {
                    if (auto* pic = qobject_cast<PictureBubble*>(bubble)) {
                        pic->setTransferState(TransferState::Completed);
                    }
                    else if (auto* file = qobject_cast<FileBubble*>(bubble)) {
                        file->setTransferState(TransferState::Completed);
                    }
                }
                _download_bubbles.erase(it);
            }

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
            if (auto it = _download_bubbles.find(name); it != _download_bubbles.end()) {
                if (QWidget* bubble = it.value()) {
                    if (auto* pic = qobject_cast<PictureBubble*>(bubble)) {
                        pic->setTransferState(TransferState::Failed);
                    }
                    else if (auto* file = qobject_cast<FileBubble*>(bubble)) {
                        file->setTransferState(TransferState::Failed);
                    }
                }
                _download_bubbles.erase(it);
            }

            auto iter = _download_fail_map.find(name);
            if (iter == _download_fail_map.end()) {
                return;
            }
            iter.value()();
            _download_fail_map.erase(iter);
            _download_done_map.remove(name);
        });

    //上传进度（day40）：1038 回执驱动，按 unique_name 找回自己发的图片/文件气泡刷进度条。
    //进度条归气泡自己画，ChatPage 只负责按名字找到它，不关心是图片还是文件
    connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_chat_upload_progress,
        this, [this](const QString& name, qint64 trans_size, qint64 total_size) {
            auto iter = _upload_bubbles.find(name);
            if (iter == _upload_bubbles.end()) {
                return;
            }
            QWidget* bubble = iter.value();
            if (bubble == nullptr) {
                //气泡已随会话切换销毁：只是没人接收进度，上传本身照常完成
                _upload_bubbles.erase(iter);
                return;
            }
            if (auto* pic_bubble = qobject_cast<PictureBubble*>(bubble)) {
                pic_bubble->setProgress(trans_size, total_size);
            }
            else if (auto* file_bubble = qobject_cast<FileBubble*>(bubble)) {
                file_bubble->setProgress(trans_size, total_size);
            }
            if (trans_size >= total_size) {
                _upload_bubbles.erase(iter);   //上传完成，不再需要跟踪
            }
        });

    //下载进度（day38）：1034 每块带回 current_size/total_size，按 unique_name 找回气泡。
    //与上传共用气泡的进度条，只是方向不同（文字显示"下载中"）
    connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_chat_download_progress,
        this, [this](const QString& name, qint64 current_size, qint64 total_size) {
            auto iter = _download_bubbles.find(name);
            if (iter == _download_bubbles.end()) {
                return;
            }
            QWidget* bubble = iter.value();
            if (bubble == nullptr) {
                _download_bubbles.erase(iter);
                return;
            }
            if (auto* pic_bubble = qobject_cast<PictureBubble*>(bubble)) {
                // 不在这里强切 Downloading：暂停后可能还有一块晚到的在途回包，
                // 强切会把"已暂停"覆盖掉，用户就看不到继续按钮了
                pic_bubble->setProgress(current_size, total_size);
            }
            else if (auto* file_bubble = qobject_cast<FileBubble*>(bubble)) {
                file_bubble->setProgress(current_size, total_size);
            }
        });

    //上传失败（token 失效/续传接不上）：把气泡切失败态，避免一直停在"上传中"
    connect(FileTcpMgr::GetInstance().get(), &FileTcpMgr::sig_chat_upload_failed,
        this, [this](const QString& name) {
            auto iter = _upload_bubbles.find(name);
            if (iter == _upload_bubbles.end()) {
                return;
            }
            if (QWidget* bubble = iter.value()) {
                if (auto* pic = qobject_cast<PictureBubble*>(bubble)) {
                    pic->setTransferState(TransferState::Failed);
                }
                else if (auto* file = qobject_cast<FileBubble*>(bubble)) {
                    file->setTransferState(TransferState::Failed);
                }
            }
            _upload_bubbles.erase(iter);
        });
}

//把图片/文件气泡的暂停/继续接到 FileTcpMgr。QPointer/QObject 生命周期由父控件管理，
//ChatPage 只在这里做一次连接（气泡每次重画都是新对象，不会重复连接）
void ChatPage::AttachTransferContext(QWidget* bubble, const QString& name, bool is_upload)
{
    if (!bubble) {
        return;
    }

    auto pause_handler = [](const QString& n, bool up) {
        if (up) {
            FileTcpMgr::GetInstance()->PauseUpload(n);
        }
        else {
            FileTcpMgr::GetInstance()->PauseDownload(n);
        }
    };
    auto resume_handler = [](const QString& n, bool up) {
        if (up) {
            FileTcpMgr::GetInstance()->ResumeUpload(n);
        }
        else {
            FileTcpMgr::GetInstance()->ResumeDownload(n);
        }
    };

    if (auto* pic = qobject_cast<PictureBubble*>(bubble)) {
        pic->setTransferContext(name, is_upload);
        connect(pic, &PictureBubble::sig_transfer_pause, this, pause_handler);
        connect(pic, &PictureBubble::sig_transfer_resume, this, resume_handler);
    }
    else if (auto* file = qobject_cast<FileBubble*>(bubble)) {
        file->setTransferContext(name, is_upload);
        connect(file, &FileBubble::sig_transfer_pause, this, pause_handler);
        connect(file, &FileBubble::sig_transfer_resume, this, resume_handler);
    }
}

// 右键气泡：弹"删除"菜单。气泡本身不知道消息 id，
// 所以创建气泡时把对应的 ChatDataBase 捕获进 lambda
void ChatPage::AttachDeleteMenu(QWidget* bubble, std::shared_ptr<ChatDataBase> msg)
{
    auto* frame = qobject_cast<BubbleFrame*>(bubble);
    if (!frame || !msg) {
        return;
    }

    connect(frame, &BubbleFrame::sig_delete_requested, this, [this, msg]() {
        QMenu menu(this);
        QAction* delete_action = menu.addAction(QStringLiteral("删除"));
        QAction* chosen = menu.exec(QCursor::pos());
        if (chosen == delete_action) {
            DeleteChatMsg(msg);
        }
    });
}

// 删除单条消息：
// 1. 从聊天页移除气泡
// 2. 从 ChatThreadData 缓存移除（重算最后一条，左侧列表内容/时间同步刷新）
// 3. 已落库的消息发 1061，服务端写 chat_message_deleted（只删自己视角）
void ChatPage::DeleteChatMsg(std::shared_ptr<ChatDataBase> msg)
{
    if (!msg || !_chat_data) {
        return;
    }

    const int msg_id = msg->GetMsgId();
    const QString unique_id = msg->GetUniqueId();
    ChatItemBase* item = nullptr;

    if (msg_id > 0) {
        auto iter = _base_item_map.find(msg_id);
        if (iter != _base_item_map.end()) {
            item = iter.value();
            _base_item_map.erase(iter);
        }
    }
    else {
        auto iter = _unrsp_item_map.find(unique_id);
        if (iter != _unrsp_item_map.end()) {
            item = iter.value();
            _unrsp_item_map.erase(iter);
        }
    }

    if (item) {
        ui->chat_data_list->removeChatItem(item);
    }

    if (msg_id > 0) {
        _chat_data->RemoveMsg(msg_id);
        TcpMgr::GetInstance()->DeleteChatMsg(msg_id);
        emit sig_chat_msg_deleted(_chat_data->GetThreadId());
    }
    else {
        // 还没收到服务端回执的消息：只删本地未回执缓存，没有可发的服务端 id
        _chat_data->RemoveUnRspMsg(unique_id);
    }
}

// 群聊里一条消息该显示谁的昵称：
// - 自己：优先 nick，没填则 name
// - 好友：优先 nick，没填则 name
// - 非好友（大群里其他成员）：客户端没有资料，兜底显示 uid，至少不会丢气泡
QString ChatPage::DisplayNameForUid(int uid)
{
    auto self_info = UserMgr::GetInstance()->GetUserInfo();
    const bool is_group = _chat_data && _chat_data->IsGroup();
    if (self_info && self_info->_uid == uid) {
        return (is_group && !self_info->_nick.isEmpty()) ? self_info->_nick : self_info->_name;
    }

    auto friend_info = UserMgr::GetInstance()->GetFriendById(uid);
    if (friend_info) {
        return (is_group && !friend_info->_nick.isEmpty())
                   ? friend_info->_nick : friend_info->_name;
    }
    return QString::number(uid);
}

// 日期分隔条：今天 / 昨天 / yyyy年M月d日。
// chat_time 为空（自己刚发、还没收到回执）时按今天处理，避免新消息没有分隔条
void ChatPage::MaybeAppendDateSeparator(const QString& chat_time)
{
    QDateTime dt = QDateTime::fromString(chat_time, "yyyy-MM-dd HH:mm:ss");
    QDate date = dt.isValid() ? dt.date() : QDate::currentDate();
    if (date == _last_sep_date) {
        return;
    }
    _last_sep_date = date;

    const QDate today = QDate::currentDate();
    QString text;
    if (date == today) {
        text = QStringLiteral("今天");
    }
    else if (date == today.addDays(-1)) {
        text = QStringLiteral("昨天");
    }
    else {
        text = date.toString(QStringLiteral("yyyy年M月d日"));
    }

    auto* sep = new QWidget();
    auto* layout = new QHBoxLayout(sep);
    layout->setContentsMargins(20, 10, 20, 10);
    auto* label = new QLabel(text, sep);
    label->setStyleSheet("color:#9aa0a6;font-size:12px;");
    label->setAlignment(Qt::AlignCenter);
    layout->addStretch();
    layout->addWidget(label);
    layout->addStretch();
    ui->chat_data_list->appendChatItem(sep);
}

// 表情面板：第一次点笑脸时懒创建，之后复用。
// 用 Qt::Popup：点面板外面会自动关闭，但点面板内部不会关，可以连续插入多个表情
void ChatPage::ToggleEmojiPanel()
{
    if (_emoji_panel.isNull()) {
        auto* panel = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
        panel->setObjectName("emojiPanel");
        panel->setStyleSheet(
            "QFrame#emojiPanel{background:#ffffff;border:1px solid #e0e0e0;}");

        auto* grid = new QGridLayout(panel);
        grid->setContentsMargins(8, 8, 8, 8);
        grid->setSpacing(2);

        // 固定用 emoji 字体：WSL 下已把 Windows 的 seguiemj.ttf 装到用户字体目录，
        // 中文输入框里也能通过 fontconfig fallback 显示这些字符
        QFont emoji_font(QStringLiteral("Segoe UI Emoji"));
        emoji_font.setPointSize(15);

        static const QStringList emojis = {
            QString::fromUtf8("😀"), QString::fromUtf8("😁"), QString::fromUtf8("😂"), QString::fromUtf8("🤣"),
            QString::fromUtf8("😃"), QString::fromUtf8("😄"), QString::fromUtf8("😅"), QString::fromUtf8("😆"),
            QString::fromUtf8("😉"), QString::fromUtf8("😊"), QString::fromUtf8("😋"), QString::fromUtf8("😎"),
            QString::fromUtf8("😍"), QString::fromUtf8("😘"), QString::fromUtf8("🙂"), QString::fromUtf8("🙃"),
            QString::fromUtf8("😢"), QString::fromUtf8("😭"), QString::fromUtf8("😡"), QString::fromUtf8("😤"),
            QString::fromUtf8("😱"), QString::fromUtf8("😴"), QString::fromUtf8("🤔"), QString::fromUtf8("🙄"),
            QString::fromUtf8("😬"), QString::fromUtf8("😳"), QString::fromUtf8("🥺"), QString::fromUtf8("😷"),
            QString::fromUtf8("🤒"), QString::fromUtf8("🤕"), QString::fromUtf8("😇"), QString::fromUtf8("🤗"),
            QString::fromUtf8("🤩"), QString::fromUtf8("🥳"), QString::fromUtf8("😏"), QString::fromUtf8("😌"),
            QString::fromUtf8("😔"), QString::fromUtf8("😪"), QString::fromUtf8("😜"), QString::fromUtf8("🤪"),
            QString::fromUtf8("👍"), QString::fromUtf8("👎"), QString::fromUtf8("👏"), QString::fromUtf8("🙏"),
            QString::fromUtf8("💪"), QString::fromUtf8("👌"), QString::fromUtf8("✌️"), QString::fromUtf8("🤝"),
            QString::fromUtf8("❤️"), QString::fromUtf8("💔"), QString::fromUtf8("💯"), QString::fromUtf8("🔥"),
            QString::fromUtf8("🎉"), QString::fromUtf8("🎊"), QString::fromUtf8("🌹"), QString::fromUtf8("🌸"),
            QString::fromUtf8("☀️"), QString::fromUtf8("🌙"), QString::fromUtf8("⭐"), QString::fromUtf8("🍀"),
            QString::fromUtf8("🎁"), QString::fromUtf8("🎂"), QString::fromUtf8("🍺"), QString::fromUtf8("☕")
        };

        int index = 0;
        for (const QString& emoji : emojis) {
            auto* btn = new QToolButton(panel);
            btn->setText(emoji);
            btn->setFont(emoji_font);
            btn->setFixedSize(34, 34);
            btn->setAutoRaise(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(
                "QToolButton{border:none;border-radius:6px;}"
                "QToolButton:hover{background:#eef3f8;}");
            connect(btn, &QToolButton::clicked, this, [this, emoji]() {
                // 插入到输入框当前光标处。这里不抢输入框焦点：
                // Qt::Popup 保持打开，用户可以连续点多个表情
                QTextCursor cursor = ui->chatEdit->textCursor();
                cursor.insertText(emoji);
                ui->chatEdit->setTextCursor(cursor);
            });
            grid->addWidget(btn, index / 8, index % 8);
            ++index;
        }

        panel->adjustSize();
        _emoji_panel = panel;
    }

    if (_emoji_panel->isVisible()) {
        _emoji_panel->hide();
        return;
    }

    // 面板放在笑脸按钮上方；上面空间不够就放到按钮下方，并做屏幕边界收敛
    _emoji_panel->adjustSize();
    const QPoint anchor = ui->emo_lb->mapToGlobal(QPoint(0, 0));
    int x = anchor.x();
    int y = anchor.y() - _emoji_panel->height() - 8;
    if (QScreen* screen = QGuiApplication::screenAt(anchor)) {
        const QRect avail = screen->availableGeometry();
        if (x + _emoji_panel->width() > avail.right()) {
            x = avail.right() - _emoji_panel->width();
        }
        if (x < avail.left()) {
            x = avail.left();
        }
        if (y < avail.top()) {
            y = anchor.y() + ui->emo_lb->height() + 8;
        }
    }
    _emoji_panel->move(x, y);

    // 延后到本次鼠标点击处理完再 show：避免 Popup 刚弹出就被同一次点击关掉
    QTimer::singleShot(0, this, [this]() {
        if (_emoji_panel) {
            _emoji_panel->show();
            _emoji_panel->raise();
        }
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

//头像变更实时刷新（1057）：当前会话里所有由该用户发出的消息气泡重刷头像
void ChatPage::RefreshAvatarForUid(int uid)
{
    if (!_chat_data) {
        return;
    }
    //当前会话里该用户发的每条消息，找到已回执气泡重刷头像。
    //图片/文件气泡的骨架同样是 ChatItemBase，RefreshAvatar 统一处理。
    //未回执消息（_unrsp_item_map）都是自己发的，1057 会跳过自己，无需处理
    for (auto& msg : _chat_data->GetMsgMapRef()) {
        if (msg->GetSendUid() != uid) {
            continue;
        }
        auto it = _base_item_map.find(msg->GetMsgId());
        if (it == _base_item_map.end()) {
            continue;
        }
        it.value()->RefreshAvatar(uid);
    }
}

//画一条消息气泡：自己发的靠右（带状态图标），对方的靠左
void ChatPage::AppendChatMsg(std::shared_ptr<ChatDataBase> msg, bool rsp)
{
    auto self_info = UserMgr::GetInstance()->GetUserInfo();

    //跨天时先插一条日期分隔条（今天/昨天/yyyy年M月d日）
    MaybeAppendDateSeparator(msg->GetChatTime());

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
            pChatItem->setUserName(DisplayNameForUid(self_info->_uid));
            pChatItem->setUserIcon(LoadAvatarPixmap(self_info->_icon, QSize(42,42), self_info->_uid));
        }
        else {
            auto friend_info = UserMgr::GetInstance()->GetFriendById(sender_uid);
            pChatItem->setUserName(DisplayNameForUid(sender_uid));
            if (friend_info) {
                pChatItem->setUserIcon(LoadAvatarPixmap(friend_info->_icon, QSize(42,42), sender_uid));
            }
            else {
                //群成员不是自己的好友：没有资料，用默认头像，昵称由 DisplayNameForUid 兜底
                pChatItem->setUserIcon(LoadAvatarPixmap(":/res/head_1.jpg", QSize(42,42)));
            }
        }

        if (is_file) {
            //文件卡片：显示原始文件名+大小；本地有文件时点击直接打开
            auto* file_bubble = new FileBubble(
                msg_info->_file_name.isEmpty() ? msg_info->_unique_name : msg_info->_file_name,
                msg_info->_total_size, role);
            //上传/下载共用同一张卡片：先挂上下文，暂停按钮才能找到对应传输任务
            AttachTransferContext(file_bubble, msg_info->_unique_name, is_self);
            if (!local_path.isEmpty() && QFileInfo::exists(local_path)) {
                file_bubble->setLocalPath(local_path);
                //自己发的文件如果还在传，重画时恢复进度/暂停态
                if (is_self && (msg_info->_transfer_state == TransferState::Uploading
                                || msg_info->_transfer_state == TransferState::Paused)) {
                    _upload_bubbles[msg_info->_unique_name] = file_bubble;
                    file_bubble->setTransferState(msg_info->_transfer_state);
                    file_bubble->setProgress(msg_info->_rsp_size, msg_info->_total_size);
                }
            }
            else {
                need_download = true;
                //点击 → 发起 1033 下载（chatfile 目录）+ 登记成功/失败回调
                connect(file_bubble, &FileBubble::sig_card_clicked, this,
                    [this, file_bubble, msg_info, sender_uid]() {
                        _download_bubbles[msg_info->_unique_name] = file_bubble;
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
                        // 已经暂停的下载：只恢复"已暂停"显示，等用户点继续再请求，
                        // 不在这里自动续下（否则暂停语义就失效了）
                        if (FileTcpMgr::GetInstance()->IsDownloadPaused(
                                msg_info->_unique_name)) {
                            file_bubble->setTransferState(TransferState::Paused);
                        }
                        else {
                            FileTcpMgr::GetInstance()->DownloadChatFile(
                                msg_info->_unique_name, sender_uid);
                        }
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
            AttachTransferContext(pic_bubble, msg_info->_unique_name, is_self);
            pChatItem->setWidget(pic_bubble);
            if (need_download) {
                //先登记气泡，1034 下载进度按名字找回它刷进度条
                _download_bubbles[msg_info->_unique_name] = pic_bubble;
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

                //暂停中的下载：切回会话时保持"已暂停"，等用户点图继续
                if (FileTcpMgr::GetInstance()->IsDownloadPaused(
                        msg_info->_unique_name)) {
                    pic_bubble->setTransferState(TransferState::Paused);
                }
                else {
                    pic_bubble->setTransferState(TransferState::Downloading);
                    FileTcpMgr::GetInstance()->DownloadChatImage(
                        msg_info->_unique_name, sender_uid);
                }
            }
            else if (is_self && (msg_info->_transfer_state == TransferState::Uploading
                                 || msg_info->_transfer_state == TransferState::Paused)) {
                //自己发的图还在传：重画后恢复进度条/暂停显示
                _upload_bubbles[msg_info->_unique_name] = pic_bubble;
                pic_bubble->setTransferState(msg_info->_transfer_state);
                pic_bubble->setProgress(msg_info->_rsp_size, msg_info->_total_size);
            }
        }

        //右键删除：图片/文件气泡也挂上，不管本地是否已下载完成
        AttachDeleteMenu(pChatItem->getBubble(), msg);

        pChatItem->setStatus(msg->GetStatus());
        ui->chat_data_list->appendChatItem(pChatItem);

        //按回执状态挂气泡索引（全部登记，删除/刷新头像时都要能找到它）
        if (rsp) {
            _base_item_map[msg->GetMsgId()] = pChatItem;
        }
        else {
            _unrsp_item_map[msg->GetUniqueId()] = pChatItem;
        }
        return;
    }

    ChatRole role;
    if (msg->GetSendUid() == self_info->_uid) {
        role = ChatRole::Self;
        ChatItemBase* pChatItem = new ChatItemBase(role);

        pChatItem->setUserName(DisplayNameForUid(self_info->_uid));
        //头像走全局缓存加载（避免重复解码）；气泡 item 不参与下载回显的自动刷新，
        //本地缺文件时后台下载，重开会话时自然显示
        pChatItem->setUserIcon(LoadAvatarPixmap(self_info->_icon, QSize(42,42), self_info->_uid));
        QWidget* pBubble = nullptr;
        //本轮只画文本气泡；图片等类型后续再加分支
        if (msg->GetMsgType() == ChatMsgType::TEXT) {
            pBubble = new TextBubble(role, msg->GetMsgContent());
        }

        pChatItem->setWidget(pBubble);
        AttachDeleteMenu(pBubble, msg);
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
        //群成员可能不是自己的好友：没有资料也不能丢气泡，昵称兜底 uid、头像用默认
        pChatItem->setUserName(DisplayNameForUid(msg->GetSendUid()));
        if (friend_info) {
            pChatItem->setUserIcon(LoadAvatarPixmap(friend_info->_icon, QSize(42,42), friend_info->_uid));
        }
        else {
            pChatItem->setUserIcon(LoadAvatarPixmap(":/res/head_1.jpg", QSize(42,42)));
        }
        QWidget* pBubble = nullptr;
        if (msg->GetMsgType() == ChatMsgType::TEXT) {
            pBubble = new TextBubble(role, msg->GetMsgContent());
        }
        pChatItem->setWidget(pBubble);
        AttachDeleteMenu(pBubble, msg);
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
            pChatItem->setUserName(DisplayNameForUid(user_info->_uid));
            pChatItem->setUserIcon(LoadAvatarPixmap(user_info->_icon, QSize(42,42), user_info->_uid));
            QWidget* bubble = nullptr;
            if (is_file) {
                auto* file_bubble = new FileBubble(msg_info->_file_name,
                    msg_info->_total_size, role);
                // 发送方本地文件就在：直接是可打开态（之前漏了这步，卡片一直显示"点击下载"）
                file_bubble->setLocalPath(msg_info->content);
                bubble = file_bubble;
            }
            else {
                // 图片气泡用原始图片构造，不用输入框的 120×80 缩略图：
                // 发送方和接收方都走 PictureBubble 同一套 160×90 等比缩放，
                // 两边气泡尺寸才能一致（之前发送方小、接收方大就是这个原因）
                QPixmap full_pix(msg_info->content);
                bubble = new PictureBubble(
                    full_pix.isNull() ? msgList[i].pixmap : full_pix, role);
            }
            pChatItem->setWidget(bubble);
            //挂断点续传上下文（点暂停/继续时按 unique_name 找回上传任务），
            //并把状态标成上传中：进度条与暂停按钮从这一刻开始显示
            AttachTransferContext(bubble, msg_info->_unique_name, true);
            msg_info->_transfer_state = TransferState::Uploading;
            msg_info->_transfer_type = TransferType::Upload;
            if (auto* pic = qobject_cast<PictureBubble*>(bubble)) {
                pic->setTransferState(TransferState::Uploading);
            }
            else if (auto* file = qobject_cast<FileBubble*>(bubble)) {
                file->setTransferState(TransferState::Uploading);
            }
            //登记到"上传中"表：1038 回执按 unique_name 找到这个气泡刷进度条
            _upload_bubbles.insert(msg_info->_unique_name, bubble);
            pChatItem->setStatus(0);
            ui->chat_data_list->appendChatItem(pChatItem);

            //挂到"未回执"表：1036 回包按 unique_id 找到它改状态；
            //消息数据同时进会话缓存（未回执区），等回包 MoveMsg 归档
            auto msg_type = is_file ? ChatMsgType::FILE : ChatMsgType::PIC;
            auto img_msg = std::make_shared<ImgChatData>(msg_info, uuidString, thread_id,
                ChatFormType::PRIVATE, msg_type, user_info->_uid, 0);
            //右键删除：未回执阶段也能删（只清本地，服务端还没 message_id）
            AttachDeleteMenu(bubble, img_msg);
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
        //右键删除：未回执阶段也能删（只清本地，服务端还没 message_id）
        AttachDeleteMenu(pBubble, txt_msg);
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
    _upload_bubbles.clear();
    _download_bubbles.clear();
    //切换会话/重画时重置日期游标，否则新会话第一条消息可能不插分隔条
    _last_sep_date = QDate();
}
