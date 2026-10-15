#ifndef HTTPMGR_H
#define HTTPMGR_H

#include "singleton.h"
#include <QString>
#include <QUrl>
#include <QObject>
#include <QNetworkAccessManager>
#include "global.h"
#include <memory>
#include <QJsonObject>
#include <QJsonDocument>

// ===== HTTP 请求管理器 =====
// 职责：把 JSON 数据 POST 给后端，拿到回复后通过"信号"通知对应界面。
// 三个基类各自的作用：
//   QObject                   —— 为了能用信号槽机制
//   Singleton<HttpMgr>        —— 提供 GetInstance()，全程序只有一个实例
//   enable_shared_from_this   —— 为了在异步回调里用 shared_from_this() 保住自己的生命周期，
//                                防止"请求还没回来，对象已经被析构"的悬空访问
class HttpMgr:public QObject, public Singleton<HttpMgr>,
                public std::enable_shared_from_this<HttpMgr>
{
    Q_OBJECT

public:
    ~HttpMgr();
    void PostHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod);
    //url → 发给哪个网址
    //json → 发给服务器什么数据
    // req_id → 这次是做什么操作
    // mod → 哪个界面发起的请求
private:
    friend class Singleton<HttpMgr>;   // 只有单例模板能调用下面的私有构造函数
    HttpMgr();
    QNetworkAccessManager _manager;    // Qt 的网络请求总入口，所有请求都从它发出

private slots:
    void slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);
    //槽函数参数数量要≤信号数量，且参数顺序要一致
    //作用：收到通用完成信号后，按 mod 把结果转发给具体的业务模块
signals:
    void sig_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);//QString res：后端返回的原始文本，后面要转成 JSON 解析
    void sig_reg_mod_finish(ReqId id, QString res, ErrorCodes err);//注册模块的信号，注册模块去接收
    void sig_reset_mod_finish(ReqId id, QString res, ErrorCodes err);//重置密码模块的信号，重置密码模块去接收
    void sig_login_mod_finish(ReqId id, QString res, ErrorCodes err);//登陆模块的信号

};
#endif // HTTPMGR_H
