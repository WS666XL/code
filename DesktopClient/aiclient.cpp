#include "aiclient.h"
#include "Logger.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace {
// 百炼 OpenAI 兼容接口
const char* kAiEndpoint =
    "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
const char* kAiModel = "qwen-plus";
const char* kApiKeyField = "ai/api_key";
}

AiClient::AiClient(QObject* parent)
    : QObject(parent), _nam(new QNetworkAccessManager(this))
{
}

AiClient* AiClient::GetInstance()
{
    static AiClient instance;
    return &instance;
}

QString AiClient::SettingsPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/ai.ini";
}

QString AiClient::ApiKey() const
{
    QSettings settings(SettingsPath(), QSettings::IniFormat);
    return settings.value(kApiKeyField).toString();
}

bool AiClient::HasApiKey() const
{
    return !ApiKey().trimmed().isEmpty();
}

void AiClient::SetApiKey(const QString& key)
{
    QSettings settings(SettingsPath(), QSettings::IniFormat);
    settings.setValue(kApiKeyField, key.trimmed());
    settings.sync();
    LLOG_INFO << "AI api key updated locally";
}

void AiClient::Ask(const QString& prompt)
{
    const QString key = ApiKey().trimmed();
    if (key.isEmpty()) {
        emit sig_error(QStringLiteral("还没有设置百炼 API Key"));
        return;
    }
    if (_reply != nullptr) {
        emit sig_error(QStringLiteral("AI 正在回复上一条，请稍候"));
        return;
    }
    if (prompt.trimmed().isEmpty()) {
        emit sig_error(QStringLiteral("消息内容为空"));
        return;
    }

    QJsonObject message;
    message["role"] = "user";
    message["content"] = prompt;

    QJsonArray messages;
    messages.append(message);

    QJsonObject root;
    root["model"] = kAiModel;
    root["messages"] = messages;
    root["temperature"] = 0.7;

    QNetworkRequest request{QUrl(QString::fromUtf8(kAiEndpoint))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    request.setTransferTimeout(60000);

    LLOG_INFO << "AI request sent, prompt length=" << prompt.size();
    _reply = _nam->post(request, QJsonDocument(root).toJson(QJsonDocument::Compact));

    connect(_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = _reply;
        _reply = nullptr;
        if (reply == nullptr) {
            return;
        }
        const QByteArray body = reply->readAll();
        const auto net_error = reply->error();
        const QString net_error_str = reply->errorString();
        reply->deleteLater();

        if (net_error != QNetworkReply::NoError) {
            LLOG_ERROR << "AI network error:" << net_error_str
                       << " body=" << QString::fromUtf8(body.left(300));
            emit sig_error(QStringLiteral("网络请求失败：%1").arg(net_error_str));
            return;
        }

        QJsonParseError parse_error{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
            LLOG_ERROR << "AI response parse failed:" << parse_error.errorString();
            emit sig_error(QStringLiteral("AI 返回格式异常"));
            return;
        }

        const QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            const QJsonObject err = obj["error"].toObject();
            const QString msg = err["message"].toString();
            LLOG_ERROR << "AI api error:" << msg;
            emit sig_error(QStringLiteral("AI 接口错误：%1").arg(msg));
            return;
        }

        const QJsonArray choices = obj["choices"].toArray();
        if (choices.isEmpty()) {
            LLOG_ERROR << "AI response has no choices";
            emit sig_error(QStringLiteral("AI 没有返回内容"));
            return;
        }
        const QString content = choices.first().toObject()
                                    .value("message").toObject()
                                    .value("content").toString();
        if (content.trimmed().isEmpty()) {
            LLOG_ERROR << "AI response content is empty";
            emit sig_error(QStringLiteral("AI 返回内容为空"));
            return;
        }

        LLOG_INFO << "AI reply received, length=" << content.size();
        emit sig_reply(content);
    });
}
