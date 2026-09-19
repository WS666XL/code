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
    //创建一个HTTP POST请求，并设置请求头和请求体
    QByteArray data = QJsonDocument(json).toJson();
    //通过url构造请求
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.length()));
    //发送请求，并处理响应, 获取自己的智能指针，构造伪闭包并增加智能指针引用计数
    // self 被 lambda 捕获后，引用计数+1，只要请求没结束，HttpMgr 就不会被销毁
    auto self = shared_from_this();
    QNetworkReply * reply = _manager.post(request, data);
    //设置信号和槽等待发送完成
    //网络请求是异步的：这里注册回调，等 reply 发出 finished 信号时才会执行下面的 lambda
    QObject::connect(reply, &QNetworkReply::finished, [reply, self, req_id, mod](){
        //处理错误的情况
        if(reply->error() != QNetworkReply::NoError){
            qDebug() << reply->errorString();
            //发送信号通知完成
            emit self->sig_http_finish(req_id, "", ErrorCodes::ERR_NETWORK, mod);
            reply->deleteLater();   // 让 Qt 在事件循环空闲时安全释放，避免在回调里直接 delete
            return;
        }

        //无错误则读回请求
        QString res = reply->readAll();   // 后端返回的原始文本，后续转 JSON 解析

        //发送信号通知完成
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
}


