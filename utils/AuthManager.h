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
#include <QtNetwork/QNetworkRequest>

class AuthManager : public QObject
{
    Q_OBJECT

public:
    explicit AuthManager(QObject *parent = nullptr);
    ~AuthManager();

    static AuthManager *instance();

    // 第三方登录认证（Yggdrasil / authlib-injector 协议）
    void authenticate(const QString &username, const QString &password, const QString &serverUrl);

    /**
     * @brief 把用户填写的地址整理成 Yggdrasil API 根地址
     *
     * 缺协议补 https、结尾斜杠补齐；只填了域名（如 littleskin.cn）时按
     * authlib-injector 约定补 /api/yggdrasil/；误把某个端点地址整条粘进来时去掉尾巴。
     * @return 规范化后的根地址；无法解析时返回空字符串
     */
    static QString normalizeAuthServerUrl(const QString &rawUrl);

    /** 检测第三方认证服务器是否可用（读取 Yggdrasil 根信息） */
    void checkAuthServer(const QString &serverUrl);

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
    /**
     * @brief 第三方认证成功
     * @param username 服务器返回的正式角色名（不是用户输入的账号，大小写可能与输入不同）
     * @param serverUrl 已规范化的认证服务器 API 根地址
     * @param uuid 角色 UUID（32 位无横线）
     * @param accessToken 访问令牌
     * @param clientToken 客户端令牌
     * @param skinUrl 角色皮肤地址（服务器未提供时为空）
     */
    void authenticationSucceeded(const QString &username, const QString &serverUrl,
                                 const QString &uuid, const QString &accessToken,
                                 const QString &clientToken, const QString &skinUrl);
    void authenticationFailed(const QString &errorMessage);

    /**
     * @brief 认证服务器连通性检测结果
     * @param serverUrl 发起检测时的地址（原样回传，便于调用方过滤）
     * @param online 是否为可用的 Yggdrasil 认证服务器
     * @param message 细节说明（服务器名或失败原因）
     */
    void authServerStatusChecked(const QString &serverUrl, bool online, const QString &message);

    // Microsoft登录相关信号
    void microsoftLoginStarted();
    void microsoftLoginProgressChanged(int progress);
    void microsoftLoginSucceeded(const QString &username, const QString &accessToken, const QString &refreshToken);
    void microsoftLoginFailed(const QString &errorMessage);
    void microsoftLoginCanceled();
    void microsoftAuthUrlReceived(const QString &authUrl);

private slots:
    void onMicrosoftTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftRefreshTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftXboxLiveTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftXboxSecureTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftMinecraftAccessTokenReplyFinished(QNetworkReply *reply);
    void onMicrosoftEntitlementsReplyFinished(QNetworkReply *reply);
    void onMicrosoftMinecraftProfileReplyFinished(QNetworkReply *reply);

private:
    /** 第三方认证一步到位：解析结果 → 取皮肤 → 发信号 */
    void onThirdPartyAuthReply(QNetworkReply *reply);
    /** 认证成功后取角色皮肤（失败不影响登录） */
    void fetchThirdPartySkin();
    /** 收尾：发出 authenticationSucceeded */
    void finishThirdPartyLogin();
    /** 拼出 API 端点：<根地址>authserver/authenticate 等 */
    QString buildEndpoint(const QString &serverUrl, const QString &apiPath) const;
    /** 统一的 JSON 请求头（含 User-Agent，部分认证服务器缺它会拒绝） */
    QNetworkRequest createJsonRequest(const QUrl &url) const;
    /** 生成随机 clientToken */
    static QString generateClientToken();

    QNetworkAccessManager *m_networkManager;
    static AuthManager *m_instance;

    // 当前认证信息
    QString m_currentUsername;
    QString m_currentServerUrl;      ///< 已规范化的 API 根地址
    QString m_currentClientToken;
    QString m_currentAccessToken;
    QString m_currentProfileName;    ///< 服务器返回的正式角色名
    QString m_currentUuid;
    QString m_currentSkinUrl;

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
