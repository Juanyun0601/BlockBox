/**
 * @file   ForumAccountService.h
 * @brief  方块盒子论坛账户服务：登录 / 注册 / 资料 / 头像 / 封面 / 密码
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 账户管理页的实验性界面用它和论坛账号打通：
 *   登录、注册、读取 / 修改个人资料、换头像、换主页封面、改密码、退出登录，
 *   以及拉取「我的主题 / 我的回复 / 我的收藏」列表。
 *
 * 会话令牌保存在 QSettings（forum/sessionToken）里，重启启动器后仍然是登录状态；
 * 论坛地址取自 SettingsManager::forumBaseUrl()，换成别的站点会自动丢弃旧令牌，
 * 避免拿着 A 站的令牌去请求 B 站。
 */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QSettings;

/** 论坛账号信息（对应 /api/me 返回的 user 字段） */
struct ForumUser
{
    int id = 0;
    QString username;     ///< 登录用的账号名
    QString displayName;  ///< 昵称，未设置时等于账号名
    QString bio;          ///< 个人简介
    QString avatar;       ///< 站内头像路径，例如 /uploads/avatar-2-xxx.png
    QString cover;        ///< 个人主页封面图路径，例如 /uploads/cover-2-xxx.png
    QString role;         ///< user / admin
    QString status;       ///< active / muted / banned
    int threadCount = 0;
    int postCount = 0;
    int points = 0;          ///< 论坛积分「方块」余额
    bool quizPassed = false; ///< 是否已通过入站答题（答题结果绑定在账号上）
    qint64 createdAt = 0;    ///< 注册时间（秒级时间戳）
    QString email;           ///< 仅自己可见
    bool emailVerified = false; ///< 邮箱是否已通过验证码验证

    bool isValid() const { return id > 0; }
};

class ForumAccountService : public QObject
{
    Q_OBJECT

public:
    using JsonHandler = std::function<void(const QJsonObject &)>;
    using FailHandler = std::function<void(const QString &message, int httpStatus)>;

    static ForumAccountService *instance();

    /** 本地是否还留着会话令牌（不代表令牌一定没过期） */
    bool hasSession() const;

    /** 会话令牌 + 已知账号信息都存在 */
    bool isLoggedIn() const;

    /** 当前账号信息，未登录时返回默认构造的空对象 */
    ForumUser user() const { return m_user; }

    /**
     * @brief 联机房间里给别的玩家看的名字：优先账号昵称，没设昵称就用账号名
     *
     * 未登录时返回空串，兜底文案由调用方的界面自己决定（各自有翻译上下文）。
     */
    QString playerName() const;

    /** 当前会话令牌 */
    QString token() const { return m_token; }

    /** 未读通知数（来自 /api/me 的 unread，随 refreshProfile 刷新） */
    int unreadCount() const { return m_unreadCount; }

    /** 论坛根地址（已去掉结尾斜杠） */
    QString baseUrl() const;

    /** 把 /api/xxx 或 /uploads/xxx 拼成完整地址 */
    QString absoluteUrl(const QString &path) const;

    // ── 账号操作 ──
    void login(const QString &username, const QString &password);
    void registerAccount(const QString &username, const QString &password);
    void logout();

    /** 拉取当前账号资料（进入页面 / 点刷新时调用） */
    void refreshProfile();

    /** 把入站答题通过成绩上报到论坛，记到当前账号上（发帖 / 评论资格跟着账号走） */
    void reportQuizPass(int score);

    /** 是否已通过入站答题：账号有记录、或本机通过（上报成功前）都算通过 */
    bool hasPassedQuiz() const;

    // ── 人机验证（滑块拼图） ──
    //
    // 论坛开了人机验证后，所有接口都要带令牌才放行。启动器拿不到 Cookie，
    // 所以走 X-Human-Token 请求头：通过一次之后令牌存本地，到期前不用再验。
    // 任何一个请求被服务端拦下来（403 + code=human_required），这里就会发
    // humanCheckRequired，界面弹验证框；通过后把被拦的请求原样重发一遍。

    /** 本地令牌还在有效期内，进社区时不用再验 */
    bool humanCheckSatisfied() const;

    /** 本地存的免验令牌（没有 / 过期就是空串） */
    QString humanCheckToken() const { return m_humanToken; }

    /** 最近一次请求被人机验证拦下来了（界面该弹验证框） */
    bool needsHumanCheck() const { return m_humanCheckPending; }

    /** 拉一道滑块验证题，成功后发 humanChallengeReady */
    void requestHumanChallenge();

    /**
     * @brief 交卷
     * @param challengeId 题号（humanChallengeReady 里的 id）
     * @param answer      拼图左边缘应该落到的位置（图片像素，不是屏幕像素）
     * @param durationMs  从按下到松手用了多少毫秒（拖得太快会被当成脚本）
     */
    void submitHumanCheck(const QString &challengeId, int answer, int durationMs);

    /** 丢掉本地令牌，下次请求会被服务端重新拦一次 */
    void clearHumanCheck();

    /**
     * @brief 用户没验直接关掉滑块：把被拦下的请求全部按「需要人机验证」回失败
     *
     * 不回失败的话那些请求就一直挂在那儿，页面会永远停在「正在连接论坛…」，
     * 看起来就像论坛连不上。
     */
    void abortHumanCheck();

    /** 修改昵称、邮箱、个人简介 */
    void updateProfile(const QString &displayName, const QString &bio, const QString &email);

    /** 往指定邮箱发送 6 位验证码（10 分钟内有效，60 秒后才能重发） */
    void sendEmailCode(const QString &email);

    /** 校验邮箱验证码：通过后绑定该邮箱并标记已验证 */
    void verifyEmailCode(const QString &email, const QString &code);

    /** 修改密码（成功后论坛会踢掉其它设备的登录） */
    void changePassword(const QString &currentPassword, const QString &newPassword);

    /** 上传头像（PNG / JPG / WebP / GIF） */
    void uploadAvatar(const QString &filePath);

    /** 删除头像，回到默认首字母头像 */
    void removeAvatar();

    /** 上传个人主页封面（PNG / JPG / WebP / GIF，建议 3:1 横图） */
    void uploadCover(const QString &filePath);

    /** 删除个人主页封面，回到主题色渐变横幅 */
    void removeCover();

    /**
     * 这类网络错误多半是抖动（连接被掐断、握手没成、超时），重新发一次通常就好了。
     * 社区页也用它判断要不要重试，所以放在公开接口里。
     */
    static bool isTransientNetworkError(int networkError);

    // ── 通用请求（页面拉列表用） ──
    void getJson(const QString &path, JsonHandler onOk, FailHandler onFail = nullptr);
    void sendJson(const QByteArray &method, const QString &path, const QJsonObject &body,
                  JsonHandler onOk, FailHandler onFail = nullptr);

signals:
    /** 账号信息变化（登录成功、资料更新、头像更新都会发） */
    void userChanged(const ForumUser &user);

    /** 令牌失效 / 被踢下线：界面应回到未登录状态 */
    void sessionExpired();

    /** 操作失败（登录失败、保存失败、网络不通等），message 可直接展示给用户 */
    void requestFailed(const QString &message, int httpStatus);

    /** 操作成功（保存资料、改密码等），message 可直接展示给用户 */
    void requestSucceeded(const QString &message);

    /** 邮箱验证码已发出，expiresIn 为有效期秒数（倒计时用） */
    void emailCodeSent(int expiresIn);

    /** 未读通知数变化（资料刷新 / 通知标记已读后） */
    void unreadChanged(int unread);

    /** 有请求被人机验证拦下了：界面应该弹出滑块验证框 */
    void humanCheckRequired();

    /** 验证题到手，字段见服务端 /api/human/challenge（bg / piece_img / piece / id） */
    void humanChallengeReady(const QJsonObject &challenge);

    /** 验证通过：令牌已存好，之前被拦的请求会自动重发 */
    void humanCheckPassed();

    /** 验证没通过 / 出题失败，message 可直接展示给用户 */
    void humanCheckFailed(const QString &message);

private:
    /**
     * 一次请求的重发器：参数是第几次尝试（从 1 开始）。
     * 重试时要换一条新连接、并退回 HTTP/1.1，所以由工厂现建请求而不是复用旧的。
     */
    using RequestFactory = std::function<QNetworkReply *(int attempt)>;

    explicit ForumAccountService(QObject *parent = nullptr);
    ~ForumAccountService() override;

    /** 发认证类请求（登录 / 注册），不带上一次的错误会话 */
    void postAuth(const QString &path, const QJsonObject &body);

    /** 认证请求的实际发送（attempt 为第几次尝试，失败后由自己调度重发） */
    void sendAuth(const QString &path, const QJsonObject &body, int attempt);

    void prepareRequest(QNetworkRequest &request, bool withSession = true,
                        bool forceHttp1 = false) const;
    void handleReply(QNetworkReply *reply, JsonHandler onOk, FailHandler onFail,
                     const RequestFactory &retry = nullptr, int attempt = 1);

    /** 等一下重新发一次：先丢掉已经挂住的连接，再退到 HTTP/1.1 */
    void scheduleRetry(const RequestFactory &retry, JsonHandler onOk, FailHandler onFail,
                       int attempt);

    /** 网络层失败翻译成用户看得懂、也照着能处理的一句话 */
    QString networkFailureMessage(int networkError, const QString &detail, int attempts) const;

    /** 从 Set-Cookie 里取出 bbf_sid 令牌 */
    static QString cookieToken(QNetworkReply *reply);

    /** 解析错误响应体 {"error": "...", "code": "..."} */
    static QString errorMessage(const QByteArray &raw, QNetworkReply *reply);

    static ForumUser parseUser(const QJsonObject &object);

    void setUser(const ForumUser &user, bool cache = true);
    void saveSession(const QString &token);
    void clearSession();
    void loadCachedUser();

    /** 验证通过后把令牌和到期时间存进 QSettings */
    void saveHumanCheck(const QString &token, qint64 expiresAt);
    void loadHumanCheck();

    /** 验证通过后把攒下来的请求重发一遍 */
    void flushHumanCheckQueue();

    /** 从错误响应体里取 code 字段（human_required / expired / wrong ...） */
    static QString errorCode(const QByteArray &raw);

    QNetworkAccessManager *m_network = nullptr;
    QSettings *m_settings = nullptr;
    QString m_token;
    QString m_tokenBaseUrl;   ///< 令牌是在哪个论坛地址上换来的
    ForumUser m_user;
    int m_unreadCount = 0;    ///< /api/me 返回的未读通知数

    QString m_humanToken;          ///< 人机验证令牌（服务端签发的，到期作废）
    qint64 m_humanExpiresAt = 0;   ///< 令牌到期时间（秒级时间戳）
    QString m_humanTokenBaseUrl;   ///< 令牌是在哪个论坛地址上换来的
    bool m_humanCheckPending = false; ///< 有人机验证拦着没放行
    QVector<std::function<void()>> m_humanReplays; ///< 被拦下的请求，验证通过后重发
};
