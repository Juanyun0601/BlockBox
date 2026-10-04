/**
 * @file   AuthManagerMicrosoft.cpp
 * @brief  微软认证模块
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "utils/AuthManager.h"

#include <functional>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

void AuthManager::microsoftAuthenticate()
{
    m_loginProgress = 0;
    m_isMicrosoftLoginCanceled = false;
    m_currentAuthStep = StepGetAuthorizationCode;

    m_microsoftState = generateRandomString(32);
    m_microsoftCodeVerifier = generateRandomString(64);
    m_microsoftCodeChallenge = generateCodeChallenge(m_microsoftCodeVerifier);

    QString authUrl = buildMicrosoftAuthUrl();

    emit microsoftAuthUrlReceived(authUrl);
    emit microsoftLoginStarted();
    emit microsoftLoginProgressChanged(10);
}

void AuthManager::microsoftAuthenticateWithCode(const QString &code)
{
    if (m_isMicrosoftLoginCanceled) {
        emit microsoftLoginCanceled();
        return;
    }

    m_loginProgress = 30;
    emit microsoftLoginProgressChanged(m_loginProgress);
    m_currentAuthStep = StepGetAccessToken;

    QUrl tokenUrl("https://login.microsoftonline.com/common/oauth2/v2.0/token");
    QNetworkRequest request(tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery query;
    query.addQueryItem("client_id", m_microsoftClientId);
    query.addQueryItem("scope", "XboxLive.signin offline_access");
    query.addQueryItem("code", code);
    query.addQueryItem("redirect_uri", m_microsoftRedirectUri);
    query.addQueryItem("grant_type", "authorization_code");
    query.addQueryItem("code_verifier", m_microsoftCodeVerifier);

    QNetworkReply *reply = m_networkManager->post(request, query.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply) {
            onMicrosoftTokenReplyFinished(reply);
        }
    });
}

void AuthManager::microsoftRefreshToken(const QString &refreshToken)
{
    if (m_isMicrosoftLoginCanceled) {
        emit microsoftLoginCanceled();
        return;
    }

    m_loginProgress = 10;
    emit microsoftLoginProgressChanged(m_loginProgress);
    m_currentAuthStep = StepRefreshToken;

    QUrl tokenUrl("https://login.microsoftonline.com/common/oauth2/v2.0/token");
    QNetworkRequest request(tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery query;
    query.addQueryItem("client_id", m_microsoftClientId);
    query.addQueryItem("scope", "XboxLive.signin offline_access");
    query.addQueryItem("refresh_token", refreshToken);
    query.addQueryItem("redirect_uri", m_microsoftRedirectUri);
    query.addQueryItem("grant_type", "refresh_token");

    QNetworkReply *reply = m_networkManager->post(request, query.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply) {
            onMicrosoftRefreshTokenReplyFinished(reply);
        }
    });
}

void AuthManager::getXboxLiveToken(const QString &accessToken)
{
    if (m_isMicrosoftLoginCanceled) {
        emit microsoftLoginCanceled();
        return;
    }

    m_loginProgress = 40;
    emit microsoftLoginProgressChanged(m_loginProgress);
    m_currentAuthStep = StepGetXboxLiveToken;

    QJsonObject requestBody;
    QJsonObject properties;
    properties["AuthMethod"] = "RPS";
    properties["SiteName"] = "user.auth.xboxlive.com";
    properties["RpsTicket"] = "d=" + accessToken;
    requestBody["Properties"] = properties;
    requestBody["RelyingParty"] = "http://auth.xboxlive.com";
    requestBody["TokenType"] = "JWT";

    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();

    QUrl url("https://user.auth.xboxlive.com/user/authenticate");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_networkManager->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply) {
            onMicrosoftXboxLiveTokenReplyFinished(reply);
        }
    });
}

void AuthManager::getXboxSecureToken(const QString &xboxLiveToken)
{
    if (m_isMicrosoftLoginCanceled) {
        emit microsoftLoginCanceled();
        return;
    }

    m_loginProgress = 50;
    emit microsoftLoginProgressChanged(m_loginProgress);
    m_currentAuthStep = StepGetXboxSecureToken;

    QJsonObject requestBody;
    QJsonObject properties;
    properties["SandboxId"] = "RETAIL";
    QJsonArray userTokens;
    userTokens.append(xboxLiveToken);
    properties["UserTokens"] = userTokens;
    requestBody["Properties"] = properties;
    requestBody["RelyingParty"] = "rp://api.minecraftservices.com/";
    requestBody["TokenType"] = "JWT";

    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();

    QUrl url("https://xsts.auth.xboxlive.com/xsts/authorize");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_networkManager->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply) {
            onMicrosoftXboxSecureTokenReplyFinished(reply);
        }
    });
}

void AuthManager::getMinecraftAccessToken(const QString &xstsToken, const QString &uhs)
{
    if (m_isMicrosoftLoginCanceled) {
        emit microsoftLoginCanceled();
        return;
    }

    m_loginProgress = 60;
    emit microsoftLoginProgressChanged(m_loginProgress);
    m_currentAuthStep = StepGetMinecraftAccessToken;

    QJsonObject requestBody;
    requestBody["identityToken"] = "XBL3.0 x=" + uhs + ";" + xstsToken;

    QJsonDocument doc(requestBody);
    QByteArray data = doc.toJson();

    QUrl url("https://api.minecraftservices.com/authentication/login_with_xbox");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_networkManager->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply) {
            onMicrosoftMinecraftAccessTokenReplyFinished(reply);
        }
    });
}

void AuthManager::validateMinecraftOwnership(const QString &minecraftAccessToken)
{
    if (m_isMicrosoftLoginCanceled) {
        emit microsoftLoginCanceled();
        return;
    }

    m_loginProgress = 70;
    emit microsoftLoginProgressChanged(m_loginProgress);
    m_currentAuthStep = StepValidateMinecraftOwnership;

    QUrl url("https://api.minecraftservices.com/entitlements");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + minecraftAccessToken).toUtf8());

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply) {
            onMicrosoftEntitlementsReplyFinished(reply);
        }
    });
}

void AuthManager::getMinecraftProfile(const QString &minecraftAccessToken)
{
    if (m_isMicrosoftLoginCanceled) {
        emit microsoftLoginCanceled();
        return;
    }

    m_loginProgress = 80;
    emit microsoftLoginProgressChanged(m_loginProgress);
    m_currentAuthStep = StepGetMinecraftProfile;

    QUrl url("https://api.minecraftservices.com/minecraft/profile");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + minecraftAccessToken).toUtf8());

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply) {
            onMicrosoftMinecraftProfileReplyFinished(reply);
        }
    });
}

void AuthManager::cancelMicrosoftLogin()
{
    m_isMicrosoftLoginCanceled = true;
    m_loginProgress = 0;
    m_currentAuthStep = StepNone;
    emit microsoftLoginCanceled();
}

int AuthManager::loginProgress() const
{
    return m_loginProgress;
}

QString AuthManager::microsoftRedirectUri() const
{
    return m_microsoftRedirectUri;
}

void AuthManager::onMicrosoftTokenReplyFinished(QNetworkReply *reply)
{
    if (m_isMicrosoftLoginCanceled) {
        reply->deleteLater();
        emit microsoftLoginCanceled();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply, [=]() {
            microsoftAuthenticate();
        });
        reply->deleteLater();
        return;
    }

    try {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject()) {
            emit microsoftLoginFailed(tr("服务器响应格式错误"));
            reply->deleteLater();
            return;
        }

        QJsonObject response = doc.object();

        if (response.contains("error")) {
            QString errorCode = response["error"].toString();
            QString errorDescription = response["error_description"].toString();
            QString errorMessage;

            if (errorCode == "invalid_grant") {
                errorMessage = tr("授权码无效或已过期，请重新登录");
            } else if (errorCode == "access_denied") {
                errorMessage = tr("用户拒绝授权");
            } else if (errorCode == "invalid_client") {
                errorMessage = tr("客户端ID无效");
            } else {
                errorMessage = tr("获取令牌失败：") + (errorDescription.isEmpty() ? errorCode : errorDescription);
            }

            emit microsoftLoginFailed(errorMessage);
            reply->deleteLater();
            return;
        }

        if (!response.contains("access_token")) {
            emit microsoftLoginFailed(tr("未获取到访问令牌"));
            reply->deleteLater();
            return;
        }

        m_accessToken = response["access_token"].toString();
        m_refreshToken = response["refresh_token"].toString();

        getXboxLiveToken(m_accessToken);
    } catch (const std::exception &e) {
        emit microsoftLoginFailed(tr("处理响应时发生异常：") + QString::fromUtf8(e.what()));
    } catch (...) {
        emit microsoftLoginFailed(tr("处理响应时发生未知异常"));
    }

    reply->deleteLater();
}

void AuthManager::onMicrosoftRefreshTokenReplyFinished(QNetworkReply *reply)
{
    if (m_isMicrosoftLoginCanceled) {
        reply->deleteLater();
        emit microsoftLoginCanceled();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply, [=]() {
            microsoftRefreshToken(m_refreshToken);
        });
        reply->deleteLater();
        return;
    }

    try {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject()) {
            emit microsoftLoginFailed(tr("服务器响应格式错误"));
            reply->deleteLater();
            return;
        }

        QJsonObject response = doc.object();

        if (response.contains("error")) {
            QString errorCode = response["error"].toString();
            QString errorDescription = response["error_description"].toString();
            QString errorMessage;

            if (errorCode == "invalid_grant") {
                microsoftAuthenticate();
                reply->deleteLater();
                return;
            } else {
                errorMessage = tr("刷新令牌失败：") + (errorDescription.isEmpty() ? errorCode : errorDescription);
            }

            emit microsoftLoginFailed(errorMessage);
            reply->deleteLater();
            return;
        }

        if (!response.contains("access_token")) {
            emit microsoftLoginFailed(tr("未获取到访问令牌"));
            reply->deleteLater();
            return;
        }

        m_accessToken = response["access_token"].toString();
        m_refreshToken = response["refresh_token"].toString();

        getXboxLiveToken(m_accessToken);
    } catch (const std::exception &e) {
        emit microsoftLoginFailed(tr("处理响应时发生异常：") + QString::fromUtf8(e.what()));
    } catch (...) {
        emit microsoftLoginFailed(tr("处理响应时发生未知异常"));
    }

    reply->deleteLater();
}

void AuthManager::onMicrosoftXboxLiveTokenReplyFinished(QNetworkReply *reply)
{
    if (m_isMicrosoftLoginCanceled) {
        reply->deleteLater();
        emit microsoftLoginCanceled();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply, [=]() {
            getXboxLiveToken(m_accessToken);
        });
        reply->deleteLater();
        return;
    }

    try {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject()) {
            emit microsoftLoginFailed(tr("服务器响应格式错误"));
            reply->deleteLater();
            return;
        }

        QJsonObject response = doc.object();

        if (!response.contains("Token")) {
            emit microsoftLoginFailed(tr("未获取到Xbox Live令牌"));
            reply->deleteLater();
            return;
        }

        QString xboxLiveToken = response["Token"].toString();

        getXboxSecureToken(xboxLiveToken);
    } catch (const std::exception &e) {
        emit microsoftLoginFailed(tr("处理响应时发生异常：") + QString::fromUtf8(e.what()));
    } catch (...) {
        emit microsoftLoginFailed(tr("处理响应时发生未知异常"));
    }

    reply->deleteLater();
}

void AuthManager::onMicrosoftXboxSecureTokenReplyFinished(QNetworkReply *reply)
{
    if (m_isMicrosoftLoginCanceled) {
        reply->deleteLater();
        emit microsoftLoginCanceled();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply, [=]() {
            getXboxSecureToken(m_xboxLiveToken);
        });
        reply->deleteLater();
        return;
    }

    try {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject()) {
            emit microsoftLoginFailed(tr("服务器响应格式错误"));
            reply->deleteLater();
            return;
        }

        QJsonObject response = doc.object();

        if (!response.contains("Token")) {
            emit microsoftLoginFailed(tr("未获取到Xbox安全令牌"));
            reply->deleteLater();
            return;
        }

        QString xstsToken = response["Token"].toString();
        QJsonValue displayClaimsValue = response["DisplayClaims"];
        if (displayClaimsValue.isObject()) {
            QJsonObject displayClaims = displayClaimsValue.toObject();
            QJsonValue xuiValue = displayClaims["xui"];
            if (xuiValue.isArray()) {
                QJsonArray xuiArray = xuiValue.toArray();
                if (xuiArray.size() > 0) {
                    QJsonValue firstXuiValue = xuiArray[0];
                    if (firstXuiValue.isObject()) {
                        QJsonObject firstXui = firstXuiValue.toObject();
                        QString uhs = firstXui["uhs"].toString();

                        getMinecraftAccessToken(xstsToken, uhs);
                    } else {
                        emit microsoftLoginFailed(tr("未获取到UHS信息"));
                    }
                } else {
                    emit microsoftLoginFailed(tr("未获取到UHS信息"));
                }
            } else {
                emit microsoftLoginFailed(tr("未获取到UHS信息"));
            }
        } else {
            emit microsoftLoginFailed(tr("未获取到UHS信息"));
        }
    } catch (const std::exception &e) {
        emit microsoftLoginFailed(tr("处理响应时发生异常：") + QString::fromUtf8(e.what()));
    } catch (...) {
        emit microsoftLoginFailed(tr("处理响应时发生未知异常"));
    }

    reply->deleteLater();
}

void AuthManager::onMicrosoftMinecraftAccessTokenReplyFinished(QNetworkReply *reply)
{
    if (m_isMicrosoftLoginCanceled) {
        reply->deleteLater();
        emit microsoftLoginCanceled();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply, [=]() {
            getMinecraftAccessToken(m_xstsToken, m_uhs);
        });
        reply->deleteLater();
        return;
    }

    try {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject()) {
            emit microsoftLoginFailed(tr("服务器响应格式错误"));
            reply->deleteLater();
            return;
        }

        QJsonObject response = doc.object();

        if (!response.contains("access_token")) {
            emit microsoftLoginFailed(tr("未获取到Minecraft访问令牌"));
            reply->deleteLater();
            return;
        }

        m_minecraftAccessToken = response["access_token"].toString();

        validateMinecraftOwnership(m_minecraftAccessToken);
    } catch (const std::exception &e) {
        emit microsoftLoginFailed(tr("处理响应时发生异常：") + QString::fromUtf8(e.what()));
    } catch (...) {
        emit microsoftLoginFailed(tr("处理响应时发生未知异常"));
    }

    reply->deleteLater();
}

void AuthManager::onMicrosoftEntitlementsReplyFinished(QNetworkReply *reply)
{
    if (m_isMicrosoftLoginCanceled) {
        reply->deleteLater();
        emit microsoftLoginCanceled();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply, [=]() {
            validateMinecraftOwnership(m_minecraftAccessToken);
        });
        reply->deleteLater();
        return;
    }

    try {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject()) {
            emit microsoftLoginFailed(tr("服务器响应格式错误"));
            reply->deleteLater();
            return;
        }

        QJsonObject response = doc.object();

        if (!response.contains("items")) {
            emit microsoftLoginFailed(tr("未获取到Minecraft所有权信息"));
            reply->deleteLater();
            return;
        }

        QJsonArray items = response["items"].toArray();
        bool hasMinecraft = false;
        for (const QJsonValue &item : items) {
            QJsonObject itemObj = item.toObject();
            QString name = itemObj["name"].toString();
            if (name == "product_minecraft" || name == "game_minecraft") {
                hasMinecraft = true;
                break;
            }
        }

        if (!hasMinecraft) {
            emit microsoftLoginFailed(tr("该账户未购买Minecraft Java Edition"));
            reply->deleteLater();
            return;
        }

        getMinecraftProfile(m_minecraftAccessToken);
    } catch (const std::exception &e) {
        emit microsoftLoginFailed(tr("处理响应时发生异常：") + QString::fromUtf8(e.what()));
    } catch (...) {
        emit microsoftLoginFailed(tr("处理响应时发生未知异常"));
    }

    reply->deleteLater();
}

void AuthManager::onMicrosoftMinecraftProfileReplyFinished(QNetworkReply *reply)
{
    if (m_isMicrosoftLoginCanceled) {
        reply->deleteLater();
        emit microsoftLoginCanceled();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply, [=]() {
            getMinecraftProfile(m_minecraftAccessToken);
        });
        reply->deleteLater();
        return;
    }

    try {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject()) {
            emit microsoftLoginFailed(tr("服务器响应格式错误"));
            reply->deleteLater();
            return;
        }

        QJsonObject response = doc.object();

        if (!response.contains("name") || !response.contains("id")) {
            emit microsoftLoginFailed(tr("未获取到Minecraft个人资料"));
            reply->deleteLater();
            return;
        }

        QString username = response["name"].toString();
        QString uuid = response["id"].toString();

        m_loginProgress = 100;
        emit microsoftLoginProgressChanged(m_loginProgress);
        emit microsoftLoginSucceeded(username, m_minecraftAccessToken, m_refreshToken);
    } catch (const std::exception &e) {
        emit microsoftLoginFailed(tr("处理响应时发生异常：") + QString::fromUtf8(e.what()));
    } catch (...) {
        emit microsoftLoginFailed(tr("处理响应时发生未知异常"));
    }

    reply->deleteLater();
}

void AuthManager::handleNetworkError(QNetworkReply *reply, std::function<void()> retryCallback)
{
    QNetworkReply::NetworkError error = reply->error();
    QString errorMessage;

    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
        errorMessage = tr("连接被拒绝，请检查网络连接或防火墙设置");
        break;
    case QNetworkReply::TimeoutError:
        errorMessage = tr("连接超时，请检查网络连接");
        break;
    case QNetworkReply::HostNotFoundError:
        errorMessage = tr("服务器未找到，请检查网络连接");
        break;
    case QNetworkReply::SslHandshakeFailedError:
        errorMessage = tr("SSL握手失败，请检查系统时间或网络环境");
        break;
    default:
        errorMessage = tr("网络错误：") + reply->errorString();
    }

    if (m_retryCount < m_maxRetries) {
        m_retryCount++;
        QTimer::singleShot(1000 * m_retryCount, this, [=]() {
            retryCallback();
        });
    } else {
        emit microsoftLoginFailed(errorMessage);
        m_retryCount = 0;
    }
}

QString AuthManager::generateRandomString(int length)
{
    const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    QString result;

    for (int i = 0; i < length; ++i) {
        int index = QRandomGenerator::global()->bounded(chars.length());
        result.append(chars.at(index));
    }

    return result;
}

QString AuthManager::generateCodeChallenge(const QString &codeVerifier)
{
    QByteArray verifierBytes = codeVerifier.toUtf8();
    QByteArray hash = QCryptographicHash::hash(verifierBytes, QCryptographicHash::Sha256);
    QString base64 = hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return base64;
}

QString AuthManager::buildMicrosoftAuthUrl()
{
    QUrl url("https://login.microsoftonline.com/common/oauth2/v2.0/authorize");
    QUrlQuery query;

    query.addQueryItem("client_id", m_microsoftClientId);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("redirect_uri", m_microsoftRedirectUri);
    query.addQueryItem("scope", "XboxLive.signin offline_access");
    query.addQueryItem("state", m_microsoftState);
    query.addQueryItem("code_challenge", m_microsoftCodeChallenge);
    query.addQueryItem("code_challenge_method", "S256");

    url.setQuery(query);
    return url.toString();
}