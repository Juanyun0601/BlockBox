/**
 * @file   AuthManager.h
 * @brief  认证管理器类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <functional>

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

class AuthManager : public QObject
{
    Q_OBJECT

public:
    explicit AuthManager(QObject *parent = nullptr);
    ~AuthManager();

    static AuthManager *instance();

    // 第三方登录认证
    void authenticate(const QString &username, const QString &password, const QString &serverUrl);
    
    // Microsoft登录认证
    void microsoftAuthenticate();
    void microsoftAuthenticateWithCode(const QString &code);
    void microsoftRefreshToken(const QString &refreshToken);
    
    // 获取登录进度
    int loginProgress() const;
    // 获取Microsoft重定向URI
    QString microsoftRedirectUri() const;

public slots:
    void cancelMicrosoftLogin();

signals:
    void authenticationSucceeded(const QString &username, const QString &serverUrl);
    void authenticationFailed(const QString &errorMessage);
    
    // Microsoft登录相关信号
    void microsoftLoginStarted();
    void microsoftLoginProgressChanged(int progress);
    void microsoftLoginSucceeded(const QString &username, const QString &accessToken, const QString &refreshToken);
    void microsoftLoginFailed(const QString &errorMessage);
    void microsoftLoginCanceled();
    void microsoftAuthUrlReceived(const QString &authUrl);

private slots:
    void onAuthReplyFinished(QNetworkReply *reply);
    void onMicrosoftTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftRefreshTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftXboxLiveTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftXboxSecureTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftMinecraftAccessTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftEntitlementsReplyFinished(QNetworkReply *reply);
    void onMicrosoftMinecraftProfileReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_networkManager;
    static AuthManager *m_instance;

    // 当前认证信息
    QString m_currentUsername;
    QString m_currentServerUrl;
    
    // Microsoft登录相关
    int m_loginProgress;
    bool m_isMicrosoftLoginCanceled;
    QString m_microsoftClientId;
    QString m_microsoftRedirectUri;
    QString m_microsoftState;
    QString m_microsoftCodeVerifier;
    QString m_microsoftCodeChallenge;
    
    // 令牌信息
    QString m_accessToken;
    QString m_refreshToken;
    QString m_xboxLiveToken;
    QString m_xstsToken;
    QString m_uhs;
    QString m_minecraftAccessToken;
    
    // 重试机制
    int m_retryCount;
    int m_maxRetries;
    
    // Microsoft认证流程步骤
    enum MicrosoftAuthStep {
        StepNone,
        StepGetAuthorizationCode,
        StepGetAccessToken,
        StepRefreshToken,
        StepGetXboxLiveToken,
        StepGetXboxSecureToken,
        StepGetMinecraftAccessToken,
        StepValidateMinecraftOwnership,
        StepGetMinecraftProfile
    };
    MicrosoftAuthStep m_currentAuthStep;
    
    // 生成随机字符串
    QString generateRandomString(int length);
    // 生成代码挑战
    QString generateCodeChallenge(const QString &codeVerifier);
    // 构建Microsoft OAuth授权URL
    QString buildMicrosoftAuthUrl();
    
    // 微软登录流程步骤
    void getXboxLiveToken(const QString &accessToken);
    void getXboxSecureToken(const QString &xboxLiveToken);
    void getMinecraftAccessToken(const QString &xstsToken, const QString &uhs);
    void validateMinecraftOwnership(const QString &minecraftAccessToken);
    void getMinecraftProfile(const QString &minecraftAccessToken);
    
    // 网络错误处理
    void handleNetworkError(QNetworkReply *reply, std::function<void()> retryCallback);
};

#endif // AUTHMANAGER_H