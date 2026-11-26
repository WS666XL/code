#ifndef AICLIENT_H
#define AICLIENT_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/******************************************************************************
 *  文件：aiclient.h
 *  作用：客户端直连百炼（DashScope）大模型
 *  说明：
 *    - API Key 只保存在本机 AppData/qingxun/ai.ini，不进代码、不进仓库
 *    - 请求走 OpenAI 兼容接口，一次只处理一条，避免并发打爆额度
 *    - 不保存聊天记录，回答只进当前会话的本地缓存
 *****************************************************************************/
class AiClient : public QObject
{
    Q_OBJECT
public:
    static AiClient* GetInstance();

    bool HasApiKey() const;
    QString ApiKey() const;
    void SetApiKey(const QString& key);

    // 发起一次问答；结果通过 sig_reply / sig_error 异步返回
    void Ask(const QString& prompt);

signals:
    void sig_reply(const QString& text);
    void sig_error(const QString& message);

private:
    explicit AiClient(QObject* parent = nullptr);

    static QString SettingsPath();

    QNetworkAccessManager* _nam = nullptr;
    QNetworkReply* _reply = nullptr;   // 非空表示上一条还没回来
};

#endif // AICLIENT_H
