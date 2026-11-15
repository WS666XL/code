#include "chatdialog.h"
#include "ui_chatdialog.h"
#include<QAction>
#include"chatuserwid.h"
#include"chatuserlist.h"
#include"loadingdlg.h"
#include<QTimer>
#include<QJsonDocument>
#include<QMovie>
#include"tcpmgr.h"
#include"usermgr.h"
#include "conuseritem.h"
#include "userinfopage.h"   // day36：用户信息页（设置入口）
#include "filetcpmgr.h"     // day39：图片聊天上传（slot_chat_img_rsp 里发起 1037）
ChatDialog::ChatDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ChatDialog),_mode(ChatUIMode::ChatMode),_state(ChatUIMode::ChatMode),_b_loading(false),
    _last_widget(nullptr),_cur_chat_thread_id(0),_loading_dlg(nullptr),_cur_load_chat(nullptr)
    //初始化列表顺序必须与成员声明顺序一致（_last_widget 声明在 _cur_chat_thread_id 前），
    //否则 -Wreorder 警告：实际初始化顺序以声明顺序为准，写错只是警告但容易埋坑
{
    ui->setupUi(this);
    ui->add_btn->SetState("normal","hover","press");
    ui->search_edit->SetMaxLength(15);

    QAction *searchAction = new QAction(ui->search_edit);
    searchAction->setIcon(QIcon(":/res/search.png"));
    ui->search_edit->addAction(searchAction,QLineEdit::LeadingPosition);
    ui->search_edit->setPlaceholderText(QStringLiteral("搜索"));
    // 创建一个清除动作并设置图标
    QAction *clearAction = new QAction(ui->search_edit);
    clearAction->setIcon(QIcon(":/res/close_transparent.png"));
    // 初始时不显示清除图标
    // 将清除动作添加到LineEdit的末尾位置
    ui->search_edit->addAction(clearAction, QLineEdit::TrailingPosition);

    // 当需要显示清除图标时，更改为实际的清除图标
    connect(ui->search_edit,&QLineEdit::textChanged,[clearAction](const QString &text){//textChanged文本有变化
        if(!text.isEmpty()){
            clearAction->setIcon(QIcon(":/res/close_search.png"));
            }
        else{
            clearAction->setIcon(QIcon(":/res/close_transparent.png"));
            }
    });
    // 连接清除动作的触发信号到槽函数，用于清除文本
    connect(clearAction, &QAction::triggered, [this, clearAction]() {
        ui->search_edit->clear();
        clearAction->setIcon(QIcon(":/res/close_transparent.png")); // 清除文本后，切换回透明图标
        ui->search_edit->clearFocus();//把输入框 `search_edit` 的输入焦点取消掉
        //清除按钮被按下则不显示搜索框
        ShowSearch(false);
    });
    ui->search_edit->SetMaxLength(15);
     ShowSearch(false);
    connect(ui->chat_user_list,&ChatUserList::sig_loading_chat_user,this,&ChatDialog::slot_loading_chat_user);
    //注意：这里不再调 addChatUserList() 塞假数据，
    //聊天列表改由服务端下发（MainWindow 切界面后调 loadChatList() 发 1025）

    // 自己的头像用登录响应里服务端下发的 icon（数据库配置），
    // 拿不到时回退到默认头像
    QString self_icon = ":/res/head_1.jpg";
    if (auto self_info = UserMgr::GetInstance()->GetUserInfo()) {
        if (!self_info->_icon.isEmpty()) {
            self_icon = self_info->_icon;
        }
    }
    ui->side_head_lb->setPixmap(LoadAvatarPixmap(ui->side_head_lb, self_icon,
        ui->side_head_lb->size()));
    ui->side_head_lb->setScaledContents(true);

    ui->side_chat_lb->setProperty("state","normal");

    ui->side_chat_lb->SetState("normal","hover","pressed","selected_normal","selected_hover","selected_pressed");

    ui->side_contact_lb->SetState("normal","hover","pressed","selected_normal","selected_hover","selected_pressed");

    AddLBGroup(ui->side_chat_lb);
    AddLBGroup(ui->side_contact_lb);

    connect(ui->side_chat_lb, &StateWidget::clicked, this, &ChatDialog::slot_side_chat);
    connect(ui->side_contact_lb, &StateWidget::clicked, this, &ChatDialog::slot_side_contact);

    //day36：第三个侧边栏按钮"设置"（入口是用户信息页）。
    //chatdialog.ui 不动，代码动态创建并插到 verticalLayout_3 末尾，
    //即排在 side_head_lb / side_chat_lb / side_contact_lb 之后，与老师版顺序一致。
    //尺寸对齐 .ui 里其他侧边栏图标的 30x30
    _side_settings_lb = new StateWidget();
    _side_settings_lb->setObjectName("side_settings_lb");   // QSS 按 objectName 匹配样式
    _side_settings_lb->setMinimumSize(30, 30);
    _side_settings_lb->setMaximumSize(30, 30);
    ui->verticalLayout_3->addWidget(_side_settings_lb);
    //状态字符串与老师版完全一致，六种状态对应 stylesheet.qss 里
    //#side_settings_lb[state='xx'] 的六条 border-image 规则
    _side_settings_lb->SetState("normal","hover","pressed","selected_normal","selected_hover","selected_pressed");
    AddLBGroup(_side_settings_lb);
    connect(_side_settings_lb, &StateWidget::clicked, this, &ChatDialog::slot_side_setting);

    //day36：用户信息页挂进 stackedWidget（第四个页面：chat_page / friend_apply_page /
    //friend_info_page / 用户信息页），保存指针供 slot_side_setting 切换
    _user_info_page = new UserInfoPage(this);
    ui->stackedWidget->addWidget(_user_info_page);
    //头像保存成功 → 侧边栏头像跟着刷新（逻辑与构造函数里首次加载一致：
    //从 UserMgr 取最新 _icon，用 LoadAvatarPixmap 走缓存重新设置）
    connect(_user_info_page, &UserInfoPage::sig_reset_head, this, [this]() {
        QString icon = ":/res/head_1.jpg";
        if (auto self_info = UserMgr::GetInstance()->GetUserInfo()) {
            if (!self_info->_icon.isEmpty()) {
                icon = self_info->_icon;
            }
        }
        ui->side_head_lb->setPixmap(LoadAvatarPixmap(ui->side_head_lb, icon,
            ui->side_head_lb->size()));
    });

    //链接输入框的变化
    connect(ui->search_edit,&QLineEdit::textEdited,this,&ChatDialog::slot_text_changed);

    //检测鼠标点击位置判断是否要清空搜索框
    this->installEventFilter(this); // 安装事件过滤器

    //设置聊天label选中状态
    ui->side_chat_lb->SetSelected(true);
    ui->stackedWidget->setCurrentWidget(ui->chat_page);
    //设置选中条目 / 聊天页：此时列表还是空的（数据走 1025 异步加载），
    //两个函数里 count<=0 会直接返回，等数据到了再真正选中
    SetSelectChatItem();
    //更新聊天界面信息
    SetSelectChatPage();

    //为searchlist 设置search edit
    ui->search_list->SetSearchEdit(ui->search_edit);

    //连接申请添加好友信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_friend_apply, this, &ChatDialog::slot_apply_friend);

    //连接认证添加好友信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_add_auth_friend, this, &ChatDialog::slot_add_auth_friend);

    //链接自己认证回复信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_auth_rsp, this,&ChatDialog::slot_auth_rsp);
    //连接searchlist跳转聊天信号
    connect(ui->search_list, &SearchList::sig_jump_chat_item, this, &ChatDialog::slot_jump_chat_item);
    //连接加载联系人的信号和槽函数
    connect(ui->con_user_list, &ContactUserList::sig_loading_contact_user,
            this, &ChatDialog::slot_loading_contact_user);

    //连接点击联系人item发出的信号和用户信息展示槽函数
    connect(ui->con_user_list, &ContactUserList::sig_switch_friend_info_page,
            this,&ChatDialog::slot_friend_info_page);
    //连接联系人页面点击好友申请条目的信号
    connect(ui->con_user_list, &ContactUserList::sig_switch_apply_friend_page,
            this,&ChatDialog::slot_switch_apply_friend_page);
    //连接好友信息界面发送的点击事件
    connect(ui->friend_info_page, &FriendInfoPage::sig_jump_chat_item, this,
            &ChatDialog::slot_jump_chat_item_from_infopage);


    //连接聊天列表点击信号
    connect(ui->chat_user_list, &QListWidget::itemClicked, this, &ChatDialog::slot_item_clicked);

    //连接对端消息通知（1019）
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_text_chat_msg,
            this, &ChatDialog::slot_text_chat_msg);

    //day39 图片聊天：1036 自己发的图入库回执 / 1039 收到对方图片
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_chat_img_rsp,
            this, &ChatDialog::slot_chat_img_rsp);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_img_chat_msg,
            this, &ChatDialog::slot_img_chat_msg);

    //连接 1026 会话列表回包
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_load_chat_thread,
            this, &ChatDialog::slot_load_chat_thread);

    //连接 1028 创建私聊回包
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_create_private_chat,
            this, &ChatDialog::slot_create_private_chat);

    //连接 1029 历史消息回包
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_load_chat_msg,
            this, &ChatDialog::slot_load_chat_msg);

    //连接 1018 自己发消息的回执（替代旧版 ChatPage 的 sig_append_send_chat_msg：
    //消息能不能送达以服务端落库后的回执为准，而不是本地发送时就当成功）
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_chat_msg_rsp, this, &ChatDialog::slot_add_chat_msg);

    // 心跳定时器：每 10 秒发一次 ID_HEART_BEAT_REQ（1023），
    // 服务端回 1024，双方都确认链路活着；被踢/掉线时心跳会失败暴露出来
    _timer = new QTimer(this);
    connect(_timer, &QTimer::timeout, this, [this]() {
        auto user_info = UserMgr::GetInstance()->GetUserInfo();
        QJsonObject textObj;
        textObj["fromuid"] = user_info->_uid;
        QJsonDocument doc(textObj);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_HEART_BEAT_REQ, jsonData);
        });

    _timer->start(10000);
}
ChatDialog::~ChatDialog()
{
    _timer->stop();
    delete ui;
}

//发 1025 拉会话列表：thread_id 传上次翻页的游标（首页 0），
//MainWindow 切到聊天界面时第一时间调用
void ChatDialog::loadChatList()
{
    showLoadingDlg(true);
    //发送请求逻辑
    QJsonObject jsonObj;
    auto uid = UserMgr::GetInstance()->GetUid();
    jsonObj["uid"] = uid;
    int last_chat_thread_id = UserMgr::GetInstance()->GetLastChatThreadId();
    jsonObj["thread_id"] = last_chat_thread_id;

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    //发送tcp请求给chat server
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_LOAD_CHAT_THREAD_REQ, jsonData);
}

//发 1029 拉历史消息：从 UserMgr 取"当前该加载的会话"，
//message_id 传该会话已加载到的最后一条消息 id（首页 0）
void ChatDialog::loadChatMsg() {

    //取当前正在加载的会话
    _cur_load_chat = UserMgr::GetInstance()->GetCurLoadData();
    if (_cur_load_chat == nullptr) {
        return;
    }

    showLoadingDlg(true);

    //发送请求逻辑
    QJsonObject jsonObj;
    jsonObj["thread_id"] = _cur_load_chat->GetThreadId();
    jsonObj["message_id"] = _cur_load_chat->GetLastMsgId();

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    //发送tcp请求给chat server
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_LOAD_CHAT_MSG_REQ, jsonData);
}

//全屏加载蒙层：show=true 弹出，false 关闭。
//为什么用它：会话列表+历史消息要多次请求-应答才能就绪，期间不能让用户操作列表
void ChatDialog::showLoadingDlg(bool show)
{
    if (show) {
        if (_loading_dlg) {
            _loading_dlg->deleteLater();
        }
        _loading_dlg = new LoadingDlg(this);
        _loading_dlg->setModal(true);
        _loading_dlg->show();
        return;
    }

    if (_loading_dlg) {
        _loading_dlg->deleteLater();
        _loading_dlg = nullptr;
    }
}

void ChatDialog::ClearLabelState(StateWidget *lb)
{
    for(auto& ele:_lb_list)
    {
        if(ele==lb)continue;

        ele->ClearState();
    }
}

bool ChatDialog::eventFilter(QObject *watched, QEvent *event)
{
    if(event->type()==QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseevent=static_cast<QMouseEvent*>(event);
        handleGlobalMousePress(mouseevent);
    }
    return QDialog::eventFilter(watched,event);
}

void ChatDialog::handleGlobalMousePress(QMouseEvent *event)
{
    // 实现点击位置的判断和处理逻辑
    // 先判断是否处于搜索模式，如果不处于搜索模式则直接返回
    if( _mode != ChatUIMode::SearchMode){
        return;
    }

    // 将鼠标点击位置转换为搜索列表坐标系中的位置
    QPoint posInSearchList =
        ui->search_list->mapFromGlobal(event->globalPosition().toPoint());
    // 判断点击位置是否在聊天列表的范围内
    if (!ui->search_list->rect().contains(posInSearchList)) {
        // 如果不在聊天列表内，清空输入框
        ui->search_edit->clear();
        ShowSearch(false);
    }
}

//收到新消息（1019）：先归档进会话缓存；
//如果消息属于当前打开的会话，再往聊天页追加气泡
void ChatDialog::slot_text_chat_msg(std::vector<std::shared_ptr<TextChatData>> msglists)
{
    for (auto& msg : msglists) {

        //更新数据：按 thread_id 找到会话缓存再归档
        auto thread_id = msg->GetThreadId();
        auto thread_data = UserMgr::GetInstance()->GetChatThreadByThreadId(thread_id);
        if (!thread_data) {
            continue;
        }

        thread_data->AddMsg(msg);

        //同步刷新左侧聊天列表条目（最后一条消息 + 时间）
        RefreshChatListItem(thread_id, thread_data);

        //不是当前会话的消息只更新缓存，不往页面上画
        if (_cur_chat_thread_id != thread_id) {
            continue;
        }

        ui->chat_page->AppendChatMsg(msg);
    }

}

//刷新左侧聊天列表里某个会话条目的显示（最后消息内容 + 时间）
void ChatDialog::RefreshChatListItem(int thread_id, std::shared_ptr<ChatThreadData> thread_data)
{
    auto find_iter = _chat_thread_items.find(thread_id);
    if (find_iter == _chat_thread_items.end()) {
        return;
    }
    auto* item_wid = qobject_cast<ChatUserWid*>(ui->chat_user_list->itemWidget(find_iter.value()));
    if (item_wid) {
        item_wid->SetChatData(thread_data);
    }
}

//1036 回包：自己发的图片消息已入库（拿到 message_id）。
//归档到会话缓存 + 当前会话刷新气泡状态；发起 1037 上传放在这里——
//不依赖气泡是否在前台，用户切走会话图片也能正常传
void ChatDialog::slot_chat_img_rsp(int thread_id, std::shared_ptr<ImgChatData> chat_data)
{
    auto thread_data = UserMgr::GetInstance()->GetChatThreadByThreadId(thread_id);
    if (thread_data) {
        //按 unique_id 把未回执区的消息挪进正式缓存（与文本消息 1018 回执同款）
        thread_data->MoveMsg(chat_data);
        RefreshChatListItem(thread_id, thread_data);
    }

    if (_cur_chat_thread_id == thread_id) {
        ui->chat_page->UpdateImgChatStatus(chat_data);
    }

    //message_id 已就绪：发起 1037 分块上传（服务端落盘完成后会通知对方）
    FileTcpMgr::GetInstance()->UploadChatImage(chat_data->_msg_info->_unique_name);
}

//1039 通知：收到对方的图片消息（文件已就绪）。归档 + 当前会话直接画气泡，
//气泡层发现本地缺图会自动发起 1033 下载并在完成后替换占位图
void ChatDialog::slot_img_chat_msg(std::shared_ptr<ImgChatData> imgchat)
{
    auto thread_id = imgchat->GetThreadId();
    auto thread_data = UserMgr::GetInstance()->GetChatThreadByThreadId(thread_id);
    if (!thread_data) {
        return;
    }

    thread_data->AddMsg(imgchat);
    RefreshChatListItem(thread_id, thread_data);

    if (_cur_chat_thread_id != thread_id) {
        return;
    }

    ui->chat_page->AppendChatMsg(imgchat);
}

void ChatDialog::UpdateChatMsg(std::vector<std::shared_ptr<TextChatData> > msgdata)
{
    for(auto & msg : msgdata){
        if(msg->GetThreadId() != _cur_chat_thread_id){
            break;
        }

        ui->chat_page->AppendChatMsg(msg);
    }
}

void ChatDialog::ShowSearch(bool bsearch)
{
    if(bsearch){
        ui->chat_user_list->hide();
        ui->con_user_list->hide();
        ui->search_list->show();
        _mode = ChatUIMode::SearchMode;
    }else if(_state == ChatUIMode::ChatMode){
        ui->chat_user_list->show();
        ui->con_user_list->hide();
        ui->search_list->hide();
        _mode = ChatUIMode::ChatMode;
    }else if(_state == ChatUIMode::ContactMode){
        ui->chat_user_list->hide();
        ui->search_list->hide();
        ui->con_user_list->show();
        _mode = ChatUIMode::ContactMode;
    }else if(_state == ChatUIMode::SettingsMode){
        //day36：设置模式时中间列展示联系人列表（老师版同款处理），
        //右侧 stackedWidget 由 slot_side_setting 切到用户信息页
        ui->chat_user_list->hide();
        ui->search_list->hide();
        ui->con_user_list->show();
        _mode = ChatUIMode::ContactMode;
    }
}

void ChatDialog::AddLBGroup(StateWidget *lb)
{
    _lb_list.push_back(lb);
}

void ChatDialog::slot_loading_chat_user()
{
    if(_b_loading){
        return;
    }

    _b_loading = true;
    //LoadingDlg *loadingDialog = new LoadingDlg(this);
    //loadingDialog->setModal(true);
    //loadingDialog->show();

    QLabel *loading_item = new QLabel(this);
    QMovie *movie=new QMovie(":/res/loading.gif");

    loading_item->setMovie(movie);
    loading_item->setFixedSize(250, 70);
    loading_item->setAlignment(Qt::AlignCenter);
    movie->setScaledSize(QSize(50,50)); // gif缩放到50*50

    QListWidgetItem *item = new QListWidgetItem;
    item->setSizeHint(QSize(250,70));
    ui->chat_user_list->addItem(item);
    ui->chat_user_list->setItemWidget(item, loading_item);
    movie->start();

    QTimer::singleShot(1000, this, [this,item]() {
        qDebug() << "add new Data to list...";
        loadMoreChatUser();
        ui->chat_user_list->takeItem(ui->chat_user_list->row(item));
        ui->chat_user_list->update();
        _b_loading = false;
    });
}

void ChatDialog::slot_side_chat()
{
    qDebug()<< "receive side chat clicked";
    ClearLabelState(ui->side_chat_lb);
    ui->stackedWidget->setCurrentWidget(ui->chat_page);//让 stackedWidget 当前显示 chat_page 这个页面。
    _state = ChatUIMode::ChatMode;
    ShowSearch(false);
    //设置选中条目：回聊天页时恢复之前选中的会话
    SetSelectChatItem(_cur_chat_thread_id);
    //更新聊天界面信息
    SetSelectChatPage(_cur_chat_thread_id);
}

void ChatDialog::slot_side_contact()
{
    qDebug()<< "receive side chat clicked";
    ClearLabelState(ui->side_contact_lb);
    ui->stackedWidget->setCurrentWidget(ui->friend_apply_page);//还没写
    _state = ChatUIMode::ContactMode;
    ShowSearch(false);
}

//day36：点"设置"图标，切到用户信息页（写法与 slot_side_chat/slot_side_contact 保持一致）
void ChatDialog::slot_side_setting()
{
    qDebug()<< "receive side setting clicked";
    //只取消其他图标的选中态，_side_settings_lb 自己进入选中样式
    ClearLabelState(_side_settings_lb);
    ui->stackedWidget->setCurrentWidget(_user_info_page);
    _state = ChatUIMode::SettingsMode;
    ShowSearch(false);
}

void ChatDialog::slot_text_changed(const QString& str)
{
    if(!str.isEmpty())
    {
        ShowSearch(true);
    }
}

void ChatDialog::slot_loading_contact_user()
{
    if(_b_loading){
        return;
    }

    _b_loading = true;
    LoadingDlg *loadingDialog = new LoadingDlg(this);
    loadingDialog->setModal(true);
    loadingDialog->show();
    qDebug() << "add new data to list.....";
    //修正：联系人列表加载应该调 loadMoreConUser（旧版误调了聊天列表的加载函数）
    loadMoreConUser();
    // 加载完成后关闭对话框
    loadingDialog->deleteLater();

    _b_loading = false;
}

void ChatDialog::slot_apply_friend(std::shared_ptr<AddFriendApply> apply)
{
    qDebug() << "receive apply friend slot, applyuid is " << apply->_from_uid << " name is "
             << apply->_name << " desc is " << apply->_desc;

    bool b_already = UserMgr::GetInstance()->AlreadyApply(apply->_from_uid);
    if (b_already) {
        return;
    }

    UserMgr::GetInstance()->AddApplyList(std::make_shared<ApplyInfo>(apply));
    ui->side_contact_lb->ShowRedPoint(true);
    ui->con_user_list->ShowRedPoint(true);
    ui->friend_apply_page->AddNewApply(apply);
}

//别人认证了我（1015 通知）：登记好友，并给会话建聊天列表条目。
//注意：本版服务端 1015 不带 thread_id/chat_datas，所以这里退而发 1027
//创建/获取现成的私聊线程，等 1028 回包后在 slot_create_private_chat 里建条目
void ChatDialog::slot_add_auth_friend(std::shared_ptr<AuthInfo> auth_info) {
    qDebug() << "receive slot_add_auth__friend uid is " << auth_info->_uid
             << " name is " << auth_info->_name << " nick is " << auth_info->_nick;

    //判断如果已经是好友则跳过
    auto bfriend = UserMgr::GetInstance()->CheckFriendById(auth_info->_uid);
    if(bfriend){
        return;
    }

    UserMgr::GetInstance()->AddFriend(auth_info);

    //服务端附带历史消息且带 thread_id 时（老师协议），直接建会话和条目
    if (auth_info->_thread_id > 0) {
        auto chat_thread_data = std::make_shared<ChatThreadData>(auth_info->_uid, auth_info->_thread_id, 0);
        UserMgr::GetInstance()->AddChatThreadData(chat_thread_data, auth_info->_uid);
        for (auto& chat_msg : auth_info->_chat_datas) {
            chat_thread_data->AppendMsg(chat_msg->GetMsgId(), chat_msg);
        }

        auto iter = _chat_thread_items.find(auth_info->_thread_id);
        if (iter != _chat_thread_items.end()) {
            return;
        }

        auto* chat_user_wid = new ChatUserWid();
        chat_user_wid->SetChatData(chat_thread_data);
        QListWidgetItem* item = new QListWidgetItem;
        item->setSizeHint(chat_user_wid->sizeHint());
        ui->chat_user_list->insertItem(0, item);
        ui->chat_user_list->setItemWidget(item, chat_user_wid);
        _chat_thread_items.insert(auth_info->_thread_id, item);
        return;
    }

    //没有 thread_id（本版服务端）：发 1027 拿现成/新建的私聊线程
    QJsonObject jsonObj;
    jsonObj["uid"] = UserMgr::GetInstance()->GetUid();
    jsonObj["other_id"] = auth_info->_uid;

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_CREATE_PRIVATE_CHAT_REQ, jsonData);
}

//我认证别人成功后的回包（1014）：处理逻辑同上
void ChatDialog::slot_auth_rsp(std::shared_ptr<AuthRsp> auth_rsp)
{
    qDebug() << "receive slot_auth_rsp uid is " << auth_rsp->_uid
             << " name is " << auth_rsp->_name << " nick is " << auth_rsp->_nick;

    // 同意好友完成，"新的朋友"未读红点随之熄灭（必须在提前 return 之前，保证每次回包都熄灭）
    ui->side_contact_lb->ShowRedPoint(false);
    ui->con_user_list->ShowRedPoint(false);

    //判断如果已经是好友则跳过
    auto bfriend = UserMgr::GetInstance()->CheckFriendById(auth_rsp->_uid);
    if(bfriend){
        return;
    }

    UserMgr::GetInstance()->AddFriend(auth_rsp);

    //服务端附带历史消息且带 thread_id 时（老师协议），直接建会话和条目
    if (auth_rsp->_thread_id > 0) {
        auto* chat_user_wid = new ChatUserWid();
        auto chat_thread_data = std::make_shared<ChatThreadData>(auth_rsp->_uid, auth_rsp->_thread_id, 0);
        UserMgr::GetInstance()->AddChatThreadData(chat_thread_data, auth_rsp->_uid);
        for (auto& chat_msg : auth_rsp->_chat_datas) {
            chat_thread_data->AppendMsg(chat_msg->GetMsgId(), chat_msg);
        }
        chat_user_wid->SetChatData(chat_thread_data);
        QListWidgetItem* item = new QListWidgetItem;
        item->setSizeHint(chat_user_wid->sizeHint());
        ui->chat_user_list->insertItem(0, item);
        ui->chat_user_list->setItemWidget(item, chat_user_wid);
        _chat_thread_items.insert(auth_rsp->_thread_id, item);
        return;
    }

    //没有 thread_id（本版服务端）：发 1027 拿现成/新建的私聊线程
    QJsonObject jsonObj;
    jsonObj["uid"] = UserMgr::GetInstance()->GetUid();
    jsonObj["other_id"] = auth_rsp->_uid;

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_CREATE_PRIVATE_CHAT_REQ, jsonData);
}

//从搜索页点某个用户"发消息"：本地已有该会话就直接跳转，
//没有则发 1027 创建私聊线程（1028 回包后再跳）
void ChatDialog::slot_jump_chat_item(std::shared_ptr<SearchInfo> si)
{
    qDebug() << "slot jump chat item " ;
    auto chat_thread_data = UserMgr::GetInstance()->GetChatThreadByUid(si->_uid);
    if (chat_thread_data) {
        auto find_iter = _chat_thread_items.find(chat_thread_data->GetThreadId());
        if (find_iter != _chat_thread_items.end()) {
            qDebug() << "jump to chat item , uid is " << si->_uid;
            ui->chat_user_list->scrollToItem(find_iter.value());
            ui->side_chat_lb->SetSelected(true);
            SetSelectChatItem(chat_thread_data->GetThreadId());
            //更新聊天界面信息
            SetSelectChatPage(chat_thread_data->GetThreadId());
            slot_side_chat();
            return;
        } //说明之前有缓存过聊天数据，只是条目没了，那么重新加进来即可
        else {
            auto* chat_user_wid = new ChatUserWid();
            chat_user_wid->SetChatData(chat_thread_data);
            QListWidgetItem* item = new QListWidgetItem;
            qDebug() << "chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
            ui->chat_user_list->insertItem(0, item);
            ui->chat_user_list->setItemWidget(item, chat_user_wid);
            _chat_thread_items.insert(chat_thread_data->GetThreadId(), item);
            ui->side_chat_lb->SetSelected(true);
            SetSelectChatItem(chat_thread_data->GetThreadId());
            //更新聊天界面信息
            SetSelectChatPage(chat_thread_data->GetThreadId());
            slot_side_chat();
            return;
        }
    }

    //如果没找到，则发送创建请求
    auto uid = UserMgr::GetInstance()->GetUid();
    QJsonObject jsonObj;
    jsonObj["uid"] = uid;
    jsonObj["other_id"] = si->_uid;

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    //发送tcp请求给chat server
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_CREATE_PRIVATE_CHAT_REQ, jsonData);
}

void ChatDialog::slot_friend_info_page(std::shared_ptr<UserInfo> user_info)
{
    qDebug()<<"receive switch friend info page sig";
    _last_widget = ui->friend_info_page;
    ui->stackedWidget->setCurrentWidget(ui->friend_info_page);
    ui->friend_info_page->SetInfo(user_info);
}

void ChatDialog::slot_switch_apply_friend_page()
{
    qDebug()<<"receive switch apply friend page sig";
    _last_widget = ui->friend_apply_page;
    ui->stackedWidget->setCurrentWidget(ui->friend_apply_page);
}

//从好友资料页点"发消息"：逻辑同 slot_jump_chat_item
void ChatDialog::slot_jump_chat_item_from_infopage(std::shared_ptr<UserInfo> user_info)
{
    qDebug() << "slot jump chat item ";
    auto chat_thread_data = UserMgr::GetInstance()->GetChatThreadByUid(user_info->_uid);
    if (chat_thread_data) {
        auto find_iter = _chat_thread_items.find(chat_thread_data->GetThreadId());
        if (find_iter != _chat_thread_items.end()) {
            qDebug() << "jump to chat item , uid is " << user_info->_uid;
            ui->chat_user_list->scrollToItem(find_iter.value());
            ui->side_chat_lb->SetSelected(true);
            SetSelectChatItem(chat_thread_data->GetThreadId());
            //更新聊天界面信息
            SetSelectChatPage(chat_thread_data->GetThreadId());
            slot_side_chat();
            return;
        } //说明之前有缓存过聊天数据，只是条目没了，那么重新加进来即可
        else {
            auto* chat_user_wid = new ChatUserWid();
            chat_user_wid->SetChatData(chat_thread_data);
            QListWidgetItem* item = new QListWidgetItem;
            qDebug() << "chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
            ui->chat_user_list->insertItem(0, item);
            ui->chat_user_list->setItemWidget(item, chat_user_wid);
            _chat_thread_items.insert(chat_thread_data->GetThreadId(), item);
            ui->side_chat_lb->SetSelected(true);
            SetSelectChatItem(chat_thread_data->GetThreadId());
            //更新聊天界面信息
            SetSelectChatPage(chat_thread_data->GetThreadId());
            slot_side_chat();
            return;
        }
    }

    //如果没找到，则发送创建请求
    auto uid = UserMgr::GetInstance()->GetUid();
    QJsonObject jsonObj;
    jsonObj["uid"] = uid;
    jsonObj["other_id"] = user_info->_uid;

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    //发送tcp请求给chat server
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_CREATE_PRIVATE_CHAT_REQ, jsonData);
}

//点击聊天列表条目：从条目绑定的 ChatThreadData 直接切聊天页（一切按 thread_id）
void ChatDialog::slot_item_clicked(QListWidgetItem *item)
{
    QWidget *widget = ui->chat_user_list->itemWidget(item); // 获取自定义widget对象
    if(!widget){
        qDebug()<< "slot item clicked widget is nullptr";
        return;
    }

    // 对自定义widget进行操作， 将item 转化为基类ListItemBase
    ListItemBase *customItem = qobject_cast<ListItemBase*>(widget);
    if(!customItem){
        qDebug()<< "slot item clicked widget is nullptr";
        return;
    }

    auto itemType = customItem->GetItemType();
    if(itemType == ListItemType::INVALID_ITEM
        || itemType == ListItemType::GROUP_TIP_ITEM){
        qDebug()<< "slot invalid item clicked ";
        return;
    }


    if(itemType == ListItemType::CHAT_USER_ITEM){
        qDebug()<< "contact user item clicked ";

        auto chat_wid = qobject_cast<ChatUserWid*>(customItem);
        auto chat_data = chat_wid->GetChatData();
        if (!chat_data) {
            return;
        }
        //跳转到聊天界面
        ui->chat_page->SetChatData(chat_data);
        _cur_chat_thread_id = chat_data->GetThreadId();
        return;
    }
}

//1018 自己发消息的回执：把"未回执"的消息挪进正式缓存（补上 msg_id），
//当前会话的气泡状态图标同步刷新
void ChatDialog::slot_add_chat_msg(int thread_id, std::vector<std::shared_ptr<TextChatData>> msglists) {
    auto chat_data = UserMgr::GetInstance()->GetChatThreadByThreadId(thread_id);
    if (chat_data == nullptr) {
        return;
    }

    //将消息放入会话缓存中管理
    for (auto& msg : msglists) {
        chat_data->MoveMsg(msg);

        if (_cur_chat_thread_id != thread_id) {
            continue;
        }
        //更新聊天界面上的状态图标
        ui->chat_page->UpdateChatStatus(msg);
    }

    //自己发的消息回执也刷新列表条目（内容不变但时间要更新）
    RefreshChatListItem(thread_id, chat_data);
}

//1026 会话列表回包：
//1. 每个私聊线程建 ChatThreadData + UserMgr 登记 + 聊天列表条目；
//2. load_more=true 继续发 1025 翻页；
//3. 全部拉完关闭蒙层并开始 loadChatMsg 预加载历史消息
void ChatDialog::slot_load_chat_thread(bool load_more, int last_thread_id,
    std::vector<std::shared_ptr<ChatThreadInfo>> chat_threads)
{
    for (auto& cti : chat_threads) {
        //先处理单聊，群聊跳过，以后添加
        if (cti->_type == "group") {
            continue;
        }

        //私聊：算出"对方"的 uid（user1/user2 里不是自己的那个）
        auto uid = UserMgr::GetInstance()->GetUid();
        auto other_uid = 0;
        if (uid == cti->_user1_id) {
            other_uid = cti->_user2_id;
        }
        else {
            other_uid = cti->_user1_id;
        }

        auto chat_thread_data = std::make_shared<ChatThreadData>(other_uid, cti->_thread_id, 0);
        UserMgr::GetInstance()->AddChatThreadData(chat_thread_data, other_uid);

        //建聊天列表条目并和 thread_id 关联
        auto* chat_user_wid = new ChatUserWid();
        chat_user_wid->SetChatData(chat_thread_data);
        QListWidgetItem* item = new QListWidgetItem;
        item->setSizeHint(chat_user_wid->sizeHint());
        ui->chat_user_list->addItem(item);
        ui->chat_user_list->setItemWidget(item, chat_user_wid);
        _chat_thread_items.insert(cti->_thread_id, item);
    }

    //记下翻页游标（下次 loadChatList 从这里继续）
    UserMgr::GetInstance()->SetLastChatThreadId(last_thread_id);

    if (load_more) {
        //还有下一页，继续发 1025
        QJsonObject jsonObj;
        auto uid = UserMgr::GetInstance()->GetUid();
        jsonObj["uid"] = uid;
        jsonObj["thread_id"] = last_thread_id;

        QJsonDocument doc(jsonObj);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

        //发送tcp请求给chat server
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_LOAD_CHAT_THREAD_REQ, jsonData);
        return;
    }

    //会话列表全部就绪：关蒙层，开始逐个会话预加载历史消息
    showLoadingDlg(false);
    loadChatMsg();
}

//1028 创建私聊回包：建会话+条目，插到列表最上面并跳转过去
void ChatDialog::slot_create_private_chat(int /*uid*/, int other_id, int thread_id)
    //uid 本版用不到（回包按 other_id 建会话），形参留名会报 -Wunused-parameter
{
    auto* chat_user_wid = new ChatUserWid();
    auto chat_thread_data = std::make_shared<ChatThreadData>(other_id, thread_id, 0);
    if (chat_thread_data == nullptr) {
        return;
    }
    UserMgr::GetInstance()->AddChatThreadData(chat_thread_data, other_id);

    chat_user_wid->SetChatData(chat_thread_data);
    QListWidgetItem* item = new QListWidgetItem;
    item->setSizeHint(chat_user_wid->sizeHint());
    qDebug() << "chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_wid);
    _chat_thread_items.insert(thread_id, item);

    ui->side_chat_lb->SetSelected(true);
    SetSelectChatItem(thread_id);
    //更新聊天界面信息
    SetSelectChatPage(thread_id);
    slot_side_chat();
    return;
}

//1029 历史消息回包：
//1. 消息装进当前会话缓存并刷新"最后一条"游标；
//2. load_more=true 继续翻当前会话的历史；
//3. 当前会话加载完 → GetNextLoadData 走下一个会话；全部加载完关蒙层并选中第一项
void ChatDialog::slot_load_chat_msg(int /*thread_id*/, int msg_id, bool load_more,
    std::vector<std::shared_ptr<ChatDataBase>> msglists)
    //thread_id 本版用不到（要加载的会话直接从 _cur_load_chat 拿），形参留名会报 -Wunused-parameter
{
    _cur_load_chat->SetLastMsgId(msg_id);
    //装载历史消息（AppendMsg 顺带刷新会话的"最后一条消息"显示）
    for (auto& chat_msg : msglists) {
        _cur_load_chat->AppendMsg(chat_msg->GetMsgId(), chat_msg);
    }

    //还有未加载完的消息，就继续翻当前会话的历史
    if (load_more) {
        //发送请求逻辑
        QJsonObject jsonObj;
        jsonObj["thread_id"] = _cur_load_chat->GetThreadId();
        jsonObj["message_id"] = _cur_load_chat->GetLastMsgId();

        QJsonDocument doc(jsonObj);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

        //发送tcp请求给chat server
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_LOAD_CHAT_MSG_REQ, jsonData);
        return;
    }

    //当前会话加载完，取下一个会话
    _cur_load_chat = UserMgr::GetInstance()->GetNextLoadData();
    //都加载完了：刷新列表条目文字，选中第一项并关蒙层
    if(!_cur_load_chat){
        //历史消息装好后，把每个列表条目的"最后一条消息"刷新一遍
        for (auto it = _chat_thread_items.begin(); it != _chat_thread_items.end(); ++it) {
            auto wid = qobject_cast<ChatUserWid*>(ui->chat_user_list->itemWidget(it.value()));
            auto thread_data = UserMgr::GetInstance()->GetChatThreadByThreadId(it.key());
            if (wid && thread_data) {
                wid->SetChatData(thread_data);
            }
        }

        //更新聊天界面信息
        SetSelectChatItem();
        SetSelectChatPage();
        showLoadingDlg(false);
        return;
    }

    //继续加载下一个会话的历史消息
    QJsonObject jsonObj;
    jsonObj["thread_id"] = _cur_load_chat->GetThreadId();
    jsonObj["message_id"] = _cur_load_chat->GetLastMsgId();

    QJsonDocument doc(jsonObj);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    //发送tcp请求给chat server
    emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_LOAD_CHAT_MSG_REQ, jsonData);
}

//选中列表条目：thread_id=0 选第一项（顺带记住它的 thread_id），
//否则在 _chat_thread_items 里找对应条目
void ChatDialog::SetSelectChatItem(int thread_id)
{
    if(ui->chat_user_list->count() <= 0){
        return;
    }

    if(thread_id == 0){
        ui->chat_user_list->setCurrentRow(0);
        QListWidgetItem *firstItem = ui->chat_user_list->item(0);
        if(!firstItem){
            return;
        }

        //转为widget
        QWidget *widget = ui->chat_user_list->itemWidget(firstItem);
        if(!widget){
            return;
        }

        auto con_item = qobject_cast<ChatUserWid*>(widget);
        if(!con_item){
            return;
        }

        //记住第一项对应的会话 id
        auto chat_data = con_item->GetChatData();
        if (chat_data) {
            _cur_chat_thread_id = chat_data->GetThreadId();
        }

        return;
    }

    auto find_iter = _chat_thread_items.find(thread_id);
    if(find_iter == _chat_thread_items.end()){
        qDebug() << "thread_id [" << thread_id << "] not found, set curent row 0";
        ui->chat_user_list->setCurrentRow(0);
        return;
    }

    ui->chat_user_list->setCurrentItem(find_iter.value());

    _cur_chat_thread_id = thread_id;
}

//把 thread_id 对应的会话数据设置到右侧聊天页
void ChatDialog::SetSelectChatPage(int thread_id)
{
    if( ui->chat_user_list->count() <= 0){
        return;
    }

    if (thread_id == 0) {
        auto item = ui->chat_user_list->item(0);
        //转为widget
        QWidget* widget = ui->chat_user_list->itemWidget(item);
        if (!widget) {
            return;
        }

        auto con_item = qobject_cast<ChatUserWid*>(widget);
        if (!con_item) {
            return;
        }

        //设置信息
        auto chat_data = con_item->GetChatData();
        ui->chat_page->SetChatData(chat_data);
        return;
    }

    auto find_iter = _chat_thread_items.find(thread_id);
    if(find_iter == _chat_thread_items.end()){
        return;
    }

    //转为widget
    QWidget *widget = ui->chat_user_list->itemWidget(find_iter.value());
    if(!widget){
        return;
    }

    //判断转化为自定义的widget
    // 对自定义widget进行操作， 将item 转化为基类ListItemBase
    ListItemBase *customItem = qobject_cast<ListItemBase*>(widget);
    if(!customItem){
        qDebug()<< "qobject_cast<ListItemBase*>(widget) is nullptr";
        return;
    }

    auto itemType = customItem->GetItemType();
    if(itemType == CHAT_USER_ITEM){
        auto con_item = qobject_cast<ChatUserWid*>(customItem);
        if(!con_item){
            return;
        }

        //设置信息
        auto chat_data = con_item->GetChatData();
        ui->chat_page->SetChatData(chat_data);

        return;
    }

}

//聊天列表滚动到底：会话列表已改为服务端下发，这里不再往里塞本地好友数据
//（1025 翻页由 slot_load_chat_thread 的 load_more 分支负责）
void ChatDialog::loadMoreChatUser()
{
}

void ChatDialog::loadMoreConUser()
{
    auto friend_list = UserMgr::GetInstance()->GetConListPerPage();
    if (friend_list.empty() == false) {
        for(auto & friend_ele : friend_list){
            auto *chat_user_wid = new ConUserItem();
            chat_user_wid->SetInfo(friend_ele->_uid,friend_ele->_name,
                                   friend_ele->_icon);
            QListWidgetItem *item = new QListWidgetItem;
            //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
            item->setSizeHint(chat_user_wid->sizeHint());
            ui->con_user_list->addItem(item);
            ui->con_user_list->setItemWidget(item, chat_user_wid);
        }

        //更新已加载条目
        UserMgr::GetInstance()->UpdateContactLoadedCount();
    }
}
