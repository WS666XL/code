#include "tcpmgr.h"
#include<QTcpSocket>
#include<QJsonDocument>
#include"usermgr.h"
#include"userdata.h"
TcpMgr::~TcpMgr()
{

}

TcpMgr::TcpMgr():_host(""),_port(0),_b_recv_pending(false),_message_id(0),_message_len(0)
{
    //注册自定义类型到元对象系统：
    //这些类型会作为信号槽参数跨对象传递，Qt6 里不注册的话队列连接会报 "Cannot queue arguments of type"
    registerMetaType();
    QObject::connect(&_socket, &QTcpSocket::connected, [&]() {
        qDebug() << "Connected to server!";
        // 连接建立后发送消息
        emit sig_con_success(true);
    });

    QObject::connect(&_socket, &QTcpSocket::readyRead, [&]() {
        // 当有数据可读时，读取所有数据
        // 读取所有数据并追加到缓冲区
        _buffer.append(_socket.readAll());

        QDataStream stream(&_buffer, QIODevice::ReadOnly);
        stream.setVersion(QDataStream::Qt_5_0);

        forever {
            //先解析头部
            if(!_b_recv_pending){
                // 检查缓冲区中的数据是否足够解析出一个消息头（消息ID + 消息长度）
                if (_buffer.size() < static_cast<int>(sizeof(quint16) * 2)) {
                    return; // 数据不够，等待更多数据
                }

                // 预读取消息ID和消息长度，但不从缓冲区中移除
                stream >> _message_id >> _message_len;

                //将buffer 中的前四个字节移除
                _buffer = _buffer.mid(sizeof(quint16) * 2);

                // 输出读取的数据
                qDebug() << "Message ID:" << _message_id << ", Length:" << _message_len;

            }

            //buffer剩余长读是否满足消息体长度，不满足则退出继续等待接受
            if(_buffer.size() < _message_len){
                _b_recv_pending = true;
                return;
            }

            _b_recv_pending = false;
            // 读取消息体
            QByteArray messageBody = _buffer.mid(0, _message_len);
            qDebug() << "receive body msg is " << messageBody ;

            _buffer = _buffer.mid(_message_len);
            handleMsg(ReqId(_message_id),_message_len, messageBody);
        }

    });

    //5.15 之后版本
          QObject::connect(&_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), [&](QAbstractSocket::SocketError socketError) {
              Q_UNUSED(socketError)
              qDebug() << "Error:" << _socket.errorString();
          });

    // 处理错误（适用于Qt 5.15之前的版本）
    // QObject::connect(&_socket, static_cast<void (QTcpSocket::*)(QTcpSocket::SocketError)>(&QTcpSocket::error),
    //                  [&](QTcpSocket::SocketError socketError) {
    //                      qDebug() << "Error:" << _socket.errorString() ;
    //                      switch (socketError) {
    //                      case QTcpSocket::ConnectionRefusedError:
    //                          qDebug() << "Connection Refused!";
    //                          emit sig_con_success(false);
    //                          break;
    //                      case QTcpSocket::RemoteHostClosedError:
    //                          qDebug() << "Remote Host Closed Connection!";
    //                          break;
    //                      case QTcpSocket::HostNotFoundError:
    //                          qDebug() << "Host Not Found!";
    //                          emit sig_con_success(false);
    //                          break;
    //                      case QTcpSocket::SocketTimeoutError:
    //                          qDebug() << "Connection Timeout!";
    //                          emit sig_con_success(false);
    //                          break;
    //                      case QTcpSocket::NetworkError:
    //                          qDebug() << "Network Error!";
    //                          break;
    //                      default:
    //                          qDebug() << "Other Error!";
    //                          break;
    //                      }
    //                  });

    // 处理连接断开
    QObject::connect(&_socket, &QTcpSocket::disconnected, [&]() {
        qDebug() << "Disconnected from server.";
        //并且发送通知到界面
        emit sig_connection_closed();
    });
    //连接发送信号用来发送数据
    QObject::connect(this, &TcpMgr::sig_send_data, this, &TcpMgr::slot_send_data);
    //关闭socket：外部调 CloseConnection() → 发 sig_close → 转到 slot_tcp_close
    connect(this, &TcpMgr::sig_close, this, &TcpMgr::slot_tcp_close);
    //注册消息
    initHandlers();
}
void TcpMgr::initHandlers()
{
    //auto self = shared_from_this();
    _handlers.insert(ID_CHAT_LOGIN_RSP, [this](ReqId id, int len, QByteArray data){
        Q_UNUSED(len);
        qDebug()<< "handle id is "<< id ;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if(jsonDoc.isNull()){
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();
        qDebug()<< "data jsonobj is " << jsonObj ;

        if(!jsonObj.contains("error")){
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Login Failed, err is Json Parse Err" << err ;
            emit sig_login_failed(err);
            return;
        }

        int err = jsonObj["error"].toInt();
        if(err != ErrorCodes::SUCCESS){
            qDebug() << "Login Failed, err is " << err ;
            emit sig_login_failed(err);
            return;
        }
        auto uid = jsonObj["uid"].toInt();
        auto name = jsonObj["name"].toString();
        auto nick = jsonObj["nick"].toString();
        auto icon = jsonObj["icon"].toString();
        auto sex = jsonObj["sex"].toInt();
        auto user_info = std::make_shared<UserInfo>(uid, name, nick, icon, sex);

        UserMgr::GetInstance()->SetUserInfo(user_info);
        UserMgr::GetInstance()->SetToken(jsonObj["token"].toString());
        if(jsonObj.contains("apply_list")){
            UserMgr::GetInstance()->AppendApplyList(jsonObj["apply_list"].toArray());
        }


        //添加好友列表
        if (jsonObj.contains("friend_list")) {
            UserMgr::GetInstance()->AppendFriendList(jsonObj["friend_list"].toArray());
        }

        emit sig_swich_chatdlg();
    });


    _handlers.insert(ID_SEARCH_USER_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "user_search Failed, err is Json Parse Err" << err;

            emit sig_user_search(nullptr);
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "user_search Failed, err is " << err;
            emit sig_user_search(nullptr);
            return;
        }
        auto search_info =  std::make_shared<SearchInfo>(jsonObj["uid"].toInt(), jsonObj["name"].toString(),
                                                        jsonObj["nick"].toString(), jsonObj["desc"].toString(),
                                                        jsonObj["sex"].toInt(), jsonObj["icon"].toString());

        emit sig_user_search(search_info);
    });

    _handlers.insert(ID_NOTIFY_ADD_FRIEND_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Login Failed, err is Json Parse Err" << err;

            emit sig_user_search(nullptr);
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Login Failed, err is " << err;
            emit sig_user_search(nullptr);
            return;
        }

        int from_uid = jsonObj["applyuid"].toInt();
        QString name = jsonObj["name"].toString();
        QString desc = jsonObj["desc"].toString();
        QString icon = jsonObj["icon"].toString();
        QString nick = jsonObj["nick"].toString();
        int sex = jsonObj["sex"].toInt();

        auto apply_info = std::make_shared<AddFriendApply>(
            from_uid, name, desc,
            icon, nick, sex);

        emit sig_friend_apply(apply_info);
        qDebug()<<"ID_NOTIFY_ADD_FRIEND_REQ";
    });
    _handlers.insert(ID_NOTIFY_AUTH_FRIEND_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Auth Friend Failed, err is " << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Auth Friend Failed, err is " << err;
            return;
        }

        int from_uid = jsonObj["fromuid"].toInt();
        QString name = jsonObj["name"].toString();
        QString nick = jsonObj["nick"].toString();
        QString icon = jsonObj["icon"].toString();
        int sex = jsonObj["sex"].toInt();

        //认证通知里附带的历史聊天消息（本版服务端不下发则为空数组，SetChatDatas 里会跳过）
        std::vector<std::shared_ptr<TextChatData>> chat_datas;
        for (const QJsonValue& data : jsonObj["chat_datas"].toArray()) {
            auto send_uid = data["sender"].toInt();
            auto msg_id = data["msg_id"].toInt();
            auto thread_id = data["thread_id"].toInt();
            auto unique_id = data["unique_id"].toString();
            auto msg_content = data["msg_content"].toString();
            QString chat_time = data["chat_time"].toString();
            auto status = data["status"].toInt();
            auto chat_data = std::make_shared<TextChatData>(msg_id, unique_id, thread_id, ChatFormType::PRIVATE,
                ChatMsgType::TEXT, msg_content, send_uid, status, chat_time);
            chat_datas.push_back(chat_data);
        }

        auto auth_info = std::make_shared<AuthInfo>(from_uid,name,
                                                    nick, icon, sex);

        auth_info->SetChatDatas(chat_datas);

        emit sig_add_auth_friend(auth_info);
        qDebug()<<"ID_NOTIFY_AUTH_FRIEND_REQ";
    });

    _handlers.insert(ID_ADD_FRIEND_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Add Friend Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Add Friend Failed, err is " << err;
            return;
        }

        qDebug() << "Add Friend Success " ;
    });
    _handlers.insert(ID_AUTH_FRIEND_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Auth Friend Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Auth Friend Failed, err is " << err;
            return;
        }

        auto name = jsonObj["name"].toString();
        auto nick = jsonObj["nick"].toString();
        auto icon = jsonObj["icon"].toString();
        auto sex = jsonObj["sex"].toInt();
        auto uid = jsonObj["uid"].toInt();

        //认证回包里附带的历史聊天消息（可能为空，SetChatDatas 里会跳过）
        std::vector<std::shared_ptr<TextChatData>> chat_datas;
        for (const QJsonValue& data : jsonObj["chat_datas"].toArray()) {
            auto send_uid = data["sender"].toInt();
            auto msg_id = data["msg_id"].toInt();
            auto thread_id = data["thread_id"].toInt();
            auto unique_id = data["unique_id"].toString();
            auto msg_content = data["msg_content"].toString();
            auto status = data["status"].toInt();
            auto chat_data = std::make_shared<TextChatData>(msg_id, unique_id, thread_id, ChatFormType::PRIVATE,
                ChatMsgType::TEXT, msg_content, send_uid, status);
            chat_datas.push_back(chat_data);
        }

        auto rsp = std::make_shared<AuthRsp>(uid, name, nick, icon, sex);
        rsp->SetChatDatas(chat_datas);
        emit sig_auth_rsp(rsp);

        qDebug() << "Auth Friend Success " ;
    });
    //1018 自己发消息的服务端回执：
    //新协议 {error, fromuid, touid, thread_id, chat_datas:[{message_id,unique_id,content,status,chat_time}]}
    _handlers.insert(ID_TEXT_CHAT_MSG_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Chat Msg Rsp Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Chat Msg Rsp Failed, err is " << err;
            return;
        }

        qDebug() << "Receive Text Chat Rsp Success ";
        //收到消息后转发给页面：thread_id 定位会话，message_id/unique_id 用于状态更新
        auto thread_id = jsonObj["thread_id"].toInt();
        auto sender = jsonObj["fromuid"].toInt();

        std::vector<std::shared_ptr<TextChatData>> chat_datas;
        for (const QJsonValue& data : jsonObj["chat_datas"].toArray()) {
            auto msg_id = data["message_id"].toInt();
            auto unique_id = data["unique_id"].toString();
            auto msg_content = data["content"].toString();
            QString chat_time = data["chat_time"].toString();
            int status = data["status"].toInt();
            auto chat_data = std::make_shared<TextChatData>(msg_id, unique_id, thread_id, ChatFormType::PRIVATE,
                ChatMsgType::TEXT, msg_content, sender, status, chat_time);
            chat_datas.push_back(chat_data);
        }

        //发送信号通知界面
        emit sig_chat_msg_rsp(thread_id, chat_datas);

      });

    //1019 对端发来的文本消息通知：
    //新协议 {error, fromuid, touid, thread_id, chat_datas:[{message_id,unique_id,content,chat_time(,status)}]}
    _handlers.insert(ID_NOTIFY_TEXT_CHAT_MSG_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Notify Chat Msg Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Notify Chat Msg Failed, err is " << err;
            return;
        }

        qDebug() << "Receive Text Chat Notify Success ";

        //收到消息后转发给页面：按 thread_id 归组整包传递
        auto thread_id = jsonObj["thread_id"].toInt();
        auto sender = jsonObj["fromuid"].toInt();

        std::vector<std::shared_ptr<TextChatData>> chat_datas;
        for (const QJsonValue& data : jsonObj["chat_datas"].toArray()) {
            auto msg_id = data["message_id"].toInt();
            auto unique_id = data["unique_id"].toString();
            auto msg_content = data["content"].toString();
            QString chat_time = data["chat_time"].toString();
            //通知包里没有 status 字段时 toInt() 默认返回 0（未读），正好符合语义
            int status = data["status"].toInt();
            auto chat_data = std::make_shared<TextChatData>(msg_id, unique_id, thread_id, ChatFormType::PRIVATE,
                ChatMsgType::TEXT, msg_content, sender, status, chat_time);
            chat_datas.push_back(chat_data);
        }

        emit sig_text_chat_msg(chat_datas);
      });

    // 被踢下线（1021）：同账号在别处登录，服务端把旧连接踢掉时发来此消息
    _handlers.insert(ID_NOTIFY_OFF_LINE_REQ,[this](ReqId id, int len, QByteArray data){
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Notify Offline Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Notify Offline Failed, err is " << err;
            return;
        }

        auto uid = jsonObj["uid"].toInt();
        qDebug() << "Receive offline Notify Success, uid is " << uid ;
        //断开连接
        //并且发送通知到界面
        emit sig_notify_offline();

    });

    // 心跳回复（1024）：服务端确认链路存活，这里只打日志
    _handlers.insert(ID_HEARTBEAT_RSP,[this](ReqId id, int len, QByteArray data){
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Heart Beat Msg Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Heart Beat Msg Failed, err is " << err;
            return;
        }

        qDebug() << "Heart Beat Msg Success";
    });

    //1026 聊天线程列表回包：
    //{error, uid, load_more, next_last_id, threads:[{thread_id,type,user1_id,user2_id}]}
    _handlers.insert(ID_LOAD_CHAT_THREAD_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "chat thread json parse failed " << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "get chat thread rsp failed, error is " << err;
            return;
        }

        qDebug() << "Receive chat thread rsp Success";

        //逐条取出线程信息，交给界面去建 ChatThreadData 和聊天列表条目
        auto thread_array = jsonObj["threads"].toArray();
        std::vector<std::shared_ptr<ChatThreadInfo>> chat_threads;
        for (const QJsonValue& value : thread_array) {
            auto cti = std::make_shared<ChatThreadInfo>();
            cti->_thread_id = value["thread_id"].toInt();
            cti->_type = value["type"].toString();
            cti->_user1_id = value["user1_id"].toInt();
            cti->_user2_id = value["user2_id"].toInt();
            chat_threads.push_back(cti);
        }

        bool load_more = jsonObj["load_more"].toBool();
        int next_last_id = jsonObj["next_last_id"].toInt();
        //发送信号通知界面
        emit sig_load_chat_thread(load_more, next_last_id, chat_threads);
    });

    //1028 创建私聊回包：{error, uid, other_id, thread_id}
    _handlers.insert(ID_CREATE_PRIVATE_CHAT_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "parse create private chat json parse failed " << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "get create private chat failed, error is " << err;
            return;
        }

        qDebug() << "Receive create private chat rsp Success";

        int uid = jsonObj["uid"].toInt();
        int other_id = jsonObj["other_id"].toInt();
        int thread_id = jsonObj["thread_id"].toInt();

        //发送信号通知界面
        emit sig_create_private_chat(uid, other_id, thread_id);
    });

    //1030 历史消息分页回包：
    //{error, thread_id, last_message_id, load_more, chat_datas:[{sender,msg_id,thread_id,unique_id,msg_content,chat_time,status,msg_type,receiver}]}
    //本轮只处理文本消息（msg_type==TEXT），图片分支留待后续
    _handlers.insert(ID_LOAD_CHAT_MSG_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "parse load chat msg json parse failed " << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "get load chat msg failed, error is " << err;
            return;
        }

        qDebug() << "Receive load chat msg rsp Success";

        int thread_id = jsonObj["thread_id"].toInt();
        int last_msg_id = jsonObj["last_message_id"].toInt();
        bool load_more = jsonObj["load_more"].toBool();

        std::vector<std::shared_ptr<ChatDataBase>> chat_datas;
        for (const QJsonValue& data : jsonObj["chat_datas"].toArray()) {
            auto send_uid = data["sender"].toInt();
            auto msg_id = data["msg_id"].toInt();
            auto thread_id = data["thread_id"].toInt();
            auto msg_content = data["msg_content"].toString();
            QString chat_time = data["chat_time"].toString();
            int status = data["status"].toInt();
            int msg_type = data["msg_type"].toInt();
            //只装文本消息：基类指针入列表，界面层再按消息类型画气泡
            if (msg_type == int(ChatMsgType::TEXT)) {
                auto chat_data = std::make_shared<TextChatData>(msg_id, thread_id, ChatFormType::PRIVATE,
                    ChatMsgType::TEXT, msg_content, send_uid, status, chat_time);
                chat_datas.push_back(chat_data);
                continue;
            }

            //图片消息（msg_type=PIC）：content 存唯一文件名，本地多半没有图片文件，
            //构造占位 MsgInfo（预览图空白），气泡层发现本地缺图会自动发起下载
            if (msg_type == int(ChatMsgType::PIC)) {
                auto msg_info = std::make_shared<MsgInfo>();
                msg_info->msgFlag = "image";
                msg_info->_unique_name = msg_content;
                msg_info->_msg_id = msg_id;
                msg_info->_sender = send_uid;
                msg_info->_receiver = UserMgr::GetInstance()->GetUid();
                auto chat_data = std::make_shared<ImgChatData>(msg_info, "",
                    thread_id, ChatFormType::PRIVATE, ChatMsgType::PIC,
                    send_uid, status, chat_time);
                chat_data->SetMsgId(msg_id);
                chat_datas.push_back(chat_data);
                continue;
            }

            //视频/文件等其他类型本轮不支持，直接跳过（不画、不存）
            qDebug() << "skip unsupported msg_type " << msg_type << " in thread " << thread_id;
        }

        //发送信号通知界面
        emit sig_load_chat_msg(thread_id, last_msg_id, load_more, chat_datas);
    });

    // 1036 图片消息回包：自己发的图片消息已入库（带服务端 message_id）。
    // 更新 MsgInfo 后通知界面改气泡状态；message_id 就绪是发起 1037 文件上传
    // 的前置条件（服务端落盘后要按 message_id 更新状态并通知对方）
    _handlers.insert(ID_IMG_CHAT_MSG_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            qDebug() << "TcpMgr img chat rsp json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "TcpMgr img chat rsp error is " << err;
            return;
        }

        auto thread_id = jsonObj["thread_id"].toInt();
        auto unique_id = jsonObj["unique_id"].toString();
        auto unique_name = jsonObj["unique_name"].toString();
        auto sender = jsonObj["fromuid"].toInt();
        auto receiver = jsonObj["touid"].toInt();
        auto msg_id = jsonObj["message_id"].toInt();
        QString chat_time = jsonObj["chat_time"].toString();
        int status = jsonObj["status"].toInt();

        auto msg_info = UserMgr::GetInstance()->GetTransFileByName(unique_name);
        if (!msg_info) {
            qDebug() << "TcpMgr img chat rsp: trans file not found:" << unique_name;
            return;
        }
        //回填 1037 上传要用的身份信息
        msg_info->_msg_id = msg_id;
        msg_info->_sender = sender;
        msg_info->_receiver = receiver;

        auto chat_data = std::make_shared<ImgChatData>(msg_info, unique_id, thread_id,
            ChatFormType::PRIVATE, ChatMsgType::PIC, sender, status, chat_time);
        chat_data->SetMsgId(msg_id);

        emit sig_chat_img_rsp(thread_id, chat_data);
    });

    // 1039 图片消息通知：对方发的图片（文件已落盘完成才通知，可立即下载）。
    // 建占位 MsgInfo + 气泡，气泡层发现本地缺图会自动发起 1033 下载
    _handlers.insert(ID_NOTIFY_IMG_CHAT_MSG_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            qDebug() << "TcpMgr notify img chat json parse failed";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj["error"].toInt() != ErrorCodes::SUCCESS) {
            qDebug() << "TcpMgr notify img chat error is " << jsonObj["error"].toInt();
            return;
        }

        auto thread_id = jsonObj["thread_id"].toInt();
        auto sender_id = jsonObj["sender_id"].toInt();
        auto message_id = jsonObj["message_id"].toInt();
        auto img_name = jsonObj["img_name"].toString();
        auto total_size = static_cast<qint64>(jsonObj["total_size"].toString().toLongLong());

        //占位 MsgInfo：content 置空（本地无文件），图片就绪后按缓存路径加载
        auto msg_info = std::make_shared<MsgInfo>();
        msg_info->msgFlag = "image";
        msg_info->_unique_name = img_name;
        msg_info->_msg_id = message_id;
        msg_info->_sender = sender_id;
        msg_info->_receiver = UserMgr::GetInstance()->GetUid();
        msg_info->_total_size = total_size;

        UserMgr::GetInstance()->AddTransFile(img_name, msg_info);

        auto img_chat_data = std::make_shared<ImgChatData>(msg_info, "",
            thread_id, ChatFormType::PRIVATE, ChatMsgType::PIC,
            sender_id, MsgStatus::READED);

        emit sig_img_chat_msg(img_chat_data);
    });

}

// 主动关闭连接：emit 内部信号，由 slot_tcp_close 在对象所属线程里真正 close
void TcpMgr::CloseConnection(){
    emit sig_close();
}

// 关闭socket：外部调 CloseConnection() → 发 sig_close → 转到 slot_tcp_close
void TcpMgr::slot_tcp_close() {
    _socket.close();
}

//统一注册信号槽里要传的自定义类型（名字必须和参数类型写法完全一致）
void TcpMgr::registerMetaType() {
    qRegisterMetaType<ServerInfo>("ServerInfo");
    qRegisterMetaType<SearchInfo>("SearchInfo");
    qRegisterMetaType<std::shared_ptr<SearchInfo>>("std::shared_ptr<SearchInfo>");

    qRegisterMetaType<AddFriendApply>("AddFriendApply");
    qRegisterMetaType<std::shared_ptr<AddFriendApply>>("std::shared_ptr<AddFriendApply>");

    qRegisterMetaType<ApplyInfo>("ApplyInfo");
    qRegisterMetaType<std::shared_ptr<AuthInfo>>("std::shared_ptr<AuthInfo>");
    qRegisterMetaType<AuthRsp>("AuthRsp");
    qRegisterMetaType<std::shared_ptr<AuthRsp>>("std::shared_ptr<AuthRsp>");
    qRegisterMetaType<UserInfo>("UserInfo");

    qRegisterMetaType<std::vector<std::shared_ptr<TextChatData>>>("std::vector<std::shared_ptr<TextChatData>>");
    qRegisterMetaType<std::vector<std::shared_ptr<ChatThreadInfo>>>("std::vector<std::shared_ptr<ChatThreadInfo>>");
    qRegisterMetaType<std::shared_ptr<ChatThreadData>>("std::shared_ptr<ChatThreadData>");
    qRegisterMetaType<std::vector<std::shared_ptr<ChatDataBase>>>("std::vector<std::shared_ptr<ChatDataBase>>");
    qRegisterMetaType<ReqId>("ReqId");
}

void TcpMgr::handleMsg(ReqId id, int len, QByteArray data)
{
    auto find_iter =  _handlers.find(id);
    if(find_iter == _handlers.end()){
        qDebug()<< "not found id ["<< id << "] to handle";
        return ;
    }

    find_iter.value()(id,len,data);
}
void TcpMgr::slot_tcp_connect(ServerInfo si)
{
    qDebug()<< "receive tcp connect signal";
    // 尝试连接到服务器
    qDebug() << "Connecting to server...";
    _host = si.Host;
    _port = static_cast<uint16_t>(si.Port.toUInt());
    _socket.connectToHost(si.Host, _port);
}
void TcpMgr::slot_send_data(ReqId reqId, QByteArray dataBytes)
{
    uint16_t id = reqId;


    // 计算长度（使用网络字节序转换）
    quint16 len = static_cast<quint16>(dataBytes.size());

    // 创建一个QByteArray用于存储要发送的所有数据
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);

    // 设置数据流使用网络字节序
    out.setByteOrder(QDataStream::BigEndian);

    // 写入ID和长度
    out << id << len;

    // 添加字符串数据
    block.append(dataBytes);

    // 发送数据
    _socket.write(block);
}