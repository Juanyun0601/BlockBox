/**
 * @file   AuthManager.cpp
 * @brief  认证管理器类实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "AuthManager.h"

#include <QMutex>
#include <QUrl>
#include <QtNetwork/QNetworkRequest>

AuthManager *AuthManager::m_instance = nullptr;
static QMutex s_authInstanceMutex;

AuthManager::AuthManager(QObject *parent) : QObject(parent)
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AuthManager::onAuthReplyFinished);

    m_loginProgress = 0;
    m_isMicrosoftLoginCanceled = false;
    m_currentAuthStep = StepNone;
    m_retryCount = 0;
    m_maxRetries = 3;

    m_microsoftClientId = "00000000-0000-0000-0000-000000000000";
    m_microsoftRedirectUri = "https://login.microsoftonline.com/common/oauth2/nativeclient";
}

AuthManager::~AuthManager()
{
    delete m_networkManager;
    m_instance = nullptr;
}

AuthManager *AuthManager::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&s_authInstanceMutex);
        if (!m_instance) {
            m_instance = new AuthManager();
        }
    }
    return m_instance;
}

void AuthManager::authenticate(const QString &username, const QString &password, const QString &serverUrl)
{
    m_currentUsername = username;
    m_currentServerUrl = serverUrl;

    QUrl authUrl(serverUrl + "authenticate");
    QNetworkRequest request(authUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject requestBody;
    QJsonObject agentObject;
    agentObject["name"] = "BlockBox";
    agentObject["version"] = "1.0";
    requestBody["agent"] = agentObject;
    requestBody["username"] = username;
    requestBody["password"] = password;

    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();

    m_networkManager->post(request, data);
}

void AuthManager::onAuthReplyFinished(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit authenticationFailed(tr("网络错误：") + reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (!doc.isObject()) {
        emit authenticationFailed(tr("服务器响应格式错误"));
        reply->deleteLater();
        return;
    }

    QJsonObject response = doc.object();

    if (response.contains("error")) {
        QString errorMessage = response["errorMessage"].toString(tr("认证失败"));
        emit authenticationFailed(errorMessage);
    } else if (response.contains("accessToken")) {
        emit authenticationSucceeded(m_currentUsername, m_currentServerUrl);
    } else {
        emit authenticationFailed(tr("认证失败：服务器响应不完整"));
    }

    reply->deleteLater();
}