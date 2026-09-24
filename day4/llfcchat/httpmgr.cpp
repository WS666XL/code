#include "httpmgr.h"

HttpMgr::~HttpMgr()
{

}

HttpMgr::HttpMgr() {

    //连接http请求和完成信号，信号槽机制保证队列消费
    //把"请求完成"信号接到自己的槽上，由槽统一分发到各个业务模块
    connect(this, &HttpMgr::sig_http_finish, this, &HttpMgr::slot_http_finish);
}

// 发送一个 HTTP POST 请求
// url    目标地址
// json   请求体（会被序列化成 JSON 文本）
// req_id 本次操作的编号，回包时原样带回
// mod    发起请求的模块，决定回包交给谁
void HttpMgr::PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod)
{
    // 创建一个HTTP POST请求，并设置请求头和请求体
    // QJsonDocument 将 QJsonObject 转为 json字符串字节流
    QByteArray data = QJsonDocument(json).toJson();

    // 通过url构造网络请求对象
    QNetworkRequest request(url);
    // 设置请求头：Content-Type = application/json，告诉后端这是json数据
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // 设置请求头：Content-Length，数据字节长度
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.length()));

    // 发送请求，并处理响应
    // 获取this的shared_ptr智能指针，捕获进lambda，增加引用计数
    // 只要网络请求回调还没执行，HttpMgr对象就不会被析构，防止野指针崩溃
    auto self = shared_from_this();

    // 发起POST异步请求，传入request和json字节数据
    QNetworkReply * reply = _manager.post(request, data);

    // 绑定finished信号到lambda回调
    // QNetworkAccessManager是异步！post只是发起请求，不会阻塞等待返回
    // 等网络收到响应/出错，reply才会触发finished信号，执行lambda
    QObject::connect(reply, &QNetworkReply::finished, [reply, self, req_id, mod](){
        // 处理网络错误（超时、断网、404等）
        if(reply->error() != QNetworkReply::NoError){
            qDebug() << reply->errorString();
            // 发射信号，通知外部：本次请求失败，带回请求ID、模块、错误码
            emit self->sig_http_finish(req_id, "", ErrorCodes::ERR_NETWORK, mod);
            reply->deleteLater();   // Qt安全释放：事件循环空闲时删除reply，禁止直接delete reply
            return;
        }
        // 无网络错误，读取后端返回的全部响应文本
        QString res = reply->readAll();
        // 发射信号通知外部：请求成功，带回响应字符串
        emit self->sig_http_finish(req_id, res, ErrorCodes::SUCCESS,mod);
        reply->deleteLater();
        return;
    });
}

// 统一的回包分发：所有请求的完成信号都先到这里，再按模块转发出去
void HttpMgr::slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod)
{
    if(mod == Modules::REGISTERMOD){
        //发送信号通知指定模块http响应结束
        emit sig_reg_mod_finish(id, res, err);
    }
    if(mod == Modules::RESETMOD){
        //发送信号通知指定模块http响应结束
        emit sig_reset_mod_finish(id, res, err);
    }
    if(mod==Modules::LOGINMOD)
    {
        emit sig_login_mod_finish(id,res,err);
    }
}


