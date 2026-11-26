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

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QSettings;
class QTimer;

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
    int followerCount = 0;   ///< 个人主页的粉丝数（关注 / 取关后由接口返回值同步）
    int followingCount = 0;  ///< 个人主页的关注数
    bool following = false;  ///< 当前登录账号是否关注了这个人（只有用户主页接口会带）
    bool quizPassed = false; ///< 是否已通过入站答题（答题结果绑定在账号上）
    qint64 createdAt = 0;    ///< 注册时间（秒级时间戳）
    QString email;           ///< 仅自己可见
    bool emailVerified = false; ///< 邮箱是否已通过验证码验证
    bool passwordSet = true;    ///< 是否设置过密码（第三方登录自动创建的账号为 false）

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
    /**
     * @brief 注册新账号
     * @param email 论坛要求邮箱时必填（注册后要收验证码）
     * @param code  邮箱收到的 6 位验证码，论坛要求邮箱时必填
     */
    void registerAccount(const QString &username, const QString &password, const QString &email,
                         const QString &code);
    void logout();

    // ── 找回账号（忘了密码 / 忘了用户名） ──

    /** 给注册时填的邮箱发一条找回验证码（论坛回 username，方便告诉用户账号叫什么） */
    void sendRecoverEmailCode(const QString &email);

    /**
     * @brief 用「邮箱 + 验证码 + 新密码」找回账号
     *
     * 成功后论坛会直接下发新会话，这里接着按登录成功处理（userChanged 也会发）。
     */
    void recoverAccount(const QString &email, const QString &code, const QString &newPassword);

    /** 问一下论坛：注册要不要填邮箱 + 验证码（未登录也能调，进注册卡前先问一次） */
    void fetchRegisterPolicy();

    // ── 第三方登录（GitHub / LittleSkin） ──
    //
    // 启动器不直接和 GitHub / LittleSkin 打交道：先向论坛申请一对设备码，
    // 打开浏览器完成授权，再凭 device_code 轮询论坛取回登录会话。
    // 这样 Secret 只留在服务端，也不用在本机注册自定义协议。

    /** 拉取当前论坛开启的第三方登录方式（登录卡按结果决定显示哪些按钮） */
    void fetchOAuthProviders();

    /** 发起第三方登录：成功后发 oauthLoginStarted，里面带要打开的浏览器地址 */
    void startOAuthLogin(const QString &provider);

    /** 放弃正在等待的第三方登录（离开登录页 / 点取消） */
    void cancelOAuthLogin();

    /** 是否正在等待浏览器完成授权 */
    bool oauthLoginBusy() const { return m_oauthBusy; }

    // ── 手机扫码登录（电脑端） ──
    //
    // 电脑端已经是登录页时，可以向论坛申请一张一次性登录票据，把它画成二维码；
    // 手机端方块盒子用顶栏的扫描图标扫这张码、在手机上点「确认登录」，
    // 电脑端轮询到「已确认」后直接落在登录状态，不用在电脑上敲密码。

    /**
     * @brief 申请一张登录二维码
     * @param deviceName 电脑端设备名（手机上会显示，用来提醒用户「是不是你自己在登录」）
     */
    void startQrLogin(const QString &deviceName);

    /** 放弃正在等待的扫码登录（关掉二维码弹窗 / 换回密码登录） */
    void cancelQrLogin();

    /** 是否正在等待手机扫码 */
    bool qrLoginBusy() const { return m_qrBusy; }

    /** 当前二维码的票据（空 = 没在等待） */
    QString qrLoginTicket() const { return m_qrTicket; }

    /** 当前二维码里编码的地址（界面上可一并显示） */
    QString qrLoginScanUrl() const { return m_qrScanUrl; }

    // ── 手机端：扫码后确认电脑登录 ──

    /**
     * @brief 扫到二维码后先核一下这张码是谁在登录
     *
     * 成功时服务端会把这张码标成「已扫码」，电脑端界面随之变成「请在手机上确认」。
     */
    void scanQrTicket(const QString &ticket);

    /**
     * @brief 手机上点「确认登录 / 取消」
     * @param approve true = 允许这台电脑登录；false = 拒绝（电脑端立刻收到「已取消」）
     */
    void answerQrTicket(const QString &ticket, bool approve);

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
     * @brief 查论坛当前还要不要人机验证、本机这份令牌还剩多久
     * @param onOk 成功回调：enabled=论坛是否开启了人机验证；verified=当前是否已通过；
     *             remaining=免验剩余秒数
     * @param onFail 失败回调（网络错误等）
     *
     * 走 /api/human/status（该接口不受人机验证门禁拦截），用于在真正发请求之前
     * 先把验证做掉（例如 AI 工作模式要用社区搜索时），避免干到一半被滑块卡住。
     */
    void fetchHumanCheckStatus(std::function<void(bool enabled, bool verified, int remaining)> onOk,
                               FailHandler onFail = nullptr);

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

    /** 注册前发验证码：还没登录时也要能发，所以走注册专用的发码接口 */
    void sendRegisterEmailCode(const QString &email);

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

    /** 注册用的验证码已发出（和上面那条分开：登录卡和账号设置各有各的倒计时） */
    void registerEmailCodeSent(int expiresIn);

    /** 找回账号的验证码已发出，username 是该邮箱对应的账号名（可能为空） */
    void recoverEmailCodeSent(int expiresIn, const QString &username);

    /** 论坛的注册规矩：要不要填邮箱并验证 / 当前是否开放注册 */
    void registerPolicyChanged(bool emailRequired, bool registerOpen);

    /** 第三方登录方式列表（providers 里每项有 id / name / enabled） */
    void oauthProvidersChanged(const QJsonArray &providers);

    /** 设备码申请成功：界面该打开 verificationUrl，并把 userCode 显示给用户备用 */
    void oauthLoginStarted(const QString &provider, const QString &verificationUrl,
                           const QString &userCode, int expiresIn);

    /** 第三方授权完成，账号已登录（userChanged 也会跟着发） */
    void oauthLoginSucceeded(const ForumUser &user);

    /** 第三方登录失败 / 超时 / 取消，message 可直接展示 */
    void oauthLoginFailed(const QString &message);

    // ── 手机扫码登录：电脑端 ──

    /**
     * @brief 二维码已拿到，界面该把它画出来
     * @param ticket    票据（轮询用，界面上不用显示）
     * @param scanUrl   编码进二维码的地址
     * @param expiresIn 有效期秒数（倒计时用）
     */
    void qrLoginStarted(const QString &ticket, const QString &scanUrl, int expiresIn);

    /**
     * @brief 扫码状态变化
     * @param status  pending（还没人扫）/ scanned（已扫码，等手机确认）/ expired（过期）
     * @param message 可直接展示的一句话；status 为 scanned 时是扫码人的昵称
     */
    void qrLoginStateChanged(const QString &status, const QString &message);

    /** 手机端确认完成，电脑端已登录（userChanged 也会跟着发） */
    void qrLoginSucceeded(const ForumUser &user);

    /** 扫码登录失败 / 被取消 / 二维码过期，message 可直接展示 */
    void qrLoginFailed(const QString &message);

    // ── 手机扫码登录：手机端 ──

    /**
     * @brief 这是一张有效的登录码，可以弹确认框了
     * @param ticket 票据
     * @param info   这张码的来历（device 电脑设备名、ip、expires_in 剩余秒数）
     */
    void qrTicketScanned(const QString &ticket, const QJsonObject &info);

    /** 手机端的确认 / 取消已提交，approved 表示这次是允许还是拒绝 */
    void qrTicketAnswered(const QString &ticket, bool approved);

    /** 扫码核验 / 提交确认失败，message 可直接展示（例如「二维码已过期」） */
    void qrTicketFailed(const QString &message);

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

    /** 发认证类请求（登录 / 注册 / 找回），不带上一次的错误会话 */
    void postAuth(const QString &path, const QJsonObject &body,
                  const QString &successMessage = QString());

    /**
     * 认证请求的实际发送（attempt 为第几次尝试，失败后由自己调度重发）
     * @param successMessage 非空时用它作为成功提示（找回账号要单独说「密码已重设」）
     */
    void sendAuth(const QString &path, const QJsonObject &body, int attempt,
                  const QString &successMessage = QString());

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

    /** 停掉设备码轮询并清掉本次登录状态 */
    void stopOAuthPolling();

    /** 轮询一次设备码状态；授权完成时在这里落地会话 */
    void pollOAuthStatus();

    /** 轮询一次二维码状态；手机端确认后在这里落地会话 */
    void pollQrLogin();

    /** 停掉二维码轮询并清掉本次扫码状态 */
    void stopQrPolling();

    /** 扫码登录的提示文案（失败原因翻译成用户能照着做的一句话） */
    void failQrLogin(const QString &message);

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

    // 第三方登录设备码轮询
    QTimer *m_oauthTimer = nullptr;    int m_oauthGeneration = 0;
    QString m_oauthProvider;
    QString m_oauthDeviceCode;
    int m_oauthIntervalMs = 3000;
    int m_oauthRemainingPolls = 0;
    bool m_oauthBusy = false;
    int m_oauthNetworkRetries = 0;

    // 手机扫码登录（电脑端等手机扫 / 手机端扫完确认）
    QTimer *m_qrTimer = nullptr;
    QString m_qrTicket;          ///< 当前等待手机扫描的票据
    QString m_qrScanUrl;         ///< 编码进二维码的地址
    int m_qrRemainingPolls = 0;  ///< 还能轮询几次（票据到期就停）
    int m_qrNetworkRetries = 0;  ///< 轮询遇到网络抖动的连续重试次数
    bool m_qrBusy = false;       ///< 正在等待手机扫码
    bool m_qrScanned = false;    ///< 手机已经扫过（避免重复发「已扫码」）
    int m_qrGeneration = 0;      ///< 连点重开时用它作废旧的回调
};
