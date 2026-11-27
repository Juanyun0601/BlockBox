/**
 * @file   ForumAccountService.cpp
 * @brief  方块盒子论坛账户服务实现
 * @author BlockBox Team
 * @date   2026-10-31
 */
#include "ForumAccountService.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include "CoverImageCache.h"
#include "SettingsManager.h"
#include "QuizBank.h"

namespace
{
constexpr auto kTokenKey = "forum/sessionToken";
constexpr auto kTokenBaseUrlKey = "forum/sessionBaseUrl";
constexpr auto kUserCacheKey = "forum/cachedUser";
constexpr auto kQuizSyncedKey = "quiz/syncedToAccount";
constexpr auto kHumanTokenKey = "forum/humanToken";
constexpr auto kHumanExpiresKey = "forum/humanExpires";
constexpr auto kHumanBaseUrlKey = "forum/humanBaseUrl";
constexpr auto kSessionCookieName = "bbf_sid";
constexpr auto kHumanHeaderName = "X-Human-Token";
constexpr int kRequestTimeoutMs = 15000;

// 扫码登录：电脑端每隔 1.5 秒问一次服务端「手机上确认了没有」。
// 手机上点完确认，电脑端几乎立刻就能落到登录状态，又不会把论坛问得太勤。
constexpr int kQrPollIntervalMs = 1500;

// 头像 / 封面的上限跟论坛服务器保持一致：超过就直接被服务器拒收（头像 512 KB、封面 3 MB），
// 所以启动器这边拿到超限的图不报错，先压到限制以内再发。
constexpr qint64 kMaxAvatarBytes = 512 * 1024;
constexpr qint64 kMaxCoverBytes = 3 * 1024 * 1024;
// 头像在论坛一侧最终也只留 256 像素，本地按这个尺寸压，发上去的包更小
constexpr int kAvatarSide = 256;
// 个人主页封面统一成 3:1，和网页版裁出来的比例一致
constexpr int kCoverWidth = 1440;
constexpr int kCoverHeight = 480;
// 读图上限：再大的文件读进内存要卡好几秒，先劝用户换一张
constexpr qint64 kMaxSourceBytes = 64 * 1024 * 1024;

/**
 * 一次请求最多发几次（含第一次）。
 * 论坛服务器偶尔会把刚建好的连接掐断，个别网络对 HTTP/2 也不友好，
 * 所以第一次失败后换一条新连接、退回 HTTP/1.1 再试，别一失败就让用户以为论坛挂了。
 */
constexpr int kMaxAttempts = 3;
constexpr int kFirstRetryDelayMs = 400;
constexpr int kSecondRetryDelayMs = 1200;

constexpr int retryDelayMs(int attempt)
{
    return attempt <= 1 ? kFirstRetryDelayMs : kSecondRetryDelayMs;
}

// ────────────────────────────────────────────────────────── 图片压缩

/** 一张处理好的图片：编码后的字节 + 对应的 MIME */
struct EncodedImage
{
    QByteArray data;
    QString mime;

    bool isValid() const { return !data.isEmpty() && !mime.isEmpty(); }
};

/** 论坛只收这几种格式，其余格式（BMP / TIFF 等）本地转成 PNG / JPG 再发 */
QString mimeForFormat(const QByteArray &format)
{
    const QByteArray lower = format.toLower();
    if (lower == "jpg" || lower == "jpeg")
        return QStringLiteral("image/jpeg");
    if (lower == "gif")
        return QStringLiteral("image/gif");
    if (lower == "webp")
        return QStringLiteral("image/webp");
    return QStringLiteral("image/png");
}

/** 论坛能直接收的格式（原样上传时用） */
bool isForumImageFormat(const QByteArray &format)
{
    const QByteArray lower = format.toLower();
    return lower == "png" || lower == "jpg" || lower == "jpeg" || lower == "webp"
           || lower == "gif";
}

QByteArray encodePngBytes(const QImage &image)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly))
        return QByteArray();
    return image.save(&buffer, "PNG") ? data : QByteArray();
}

QByteArray encodeJpegBytes(const QImage &image, int quality)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly))
        return QByteArray();
    return image.save(&buffer, "JPG", quality) ? data : QByteArray();
}

/**
 * 这张图有没有真正用到透明。
 * 很多照片存成 PNG，带 alpha 通道但全是不透明的，按 PNG 编码会白胖一大圈。
 */
bool usesTransparency(const QImage &image)
{
    if (!image.hasAlphaChannel())
        return false;
    const QImage probe = image.scaled(48, 48, Qt::KeepAspectRatio, Qt::FastTransformation)
                             .convertToFormat(QImage::Format_ARGB32);
    if (probe.isNull())
        return true;
    for (int y = 0; y < probe.height(); ++y)
    {
        const QRgb *row = reinterpret_cast<const QRgb *>(probe.constScanLine(y));
        for (int x = 0; x < probe.width(); ++x)
        {
            if (qAlpha(row[x]) < 255)
                return true;
        }
    }
    return false;
}

/** 按边长缩图（只缩不放，保持比例） */
QImage fitImage(const QImage &source, int maxSide)
{
    if (source.isNull())
        return QImage();
    if (maxSide <= 0 || (source.width() <= maxSide && source.height() <= maxSide))
        return source;
    return source.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

/**
 * 读一张图，超大图按目标尺寸解码（手机照片动辄几千万像素，全解出来要几百 MB 内存）。
 * 动图（GIF / 动 WebP）只取第一帧。
 */
QImage readImageForUpload(const QString &path, int maxSide)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);   // 手机竖拍的照片带方向标记，先转正再压
    const QSize size = reader.size();
    if (size.isValid() && maxSide > 0
        && (size.width() > maxSide * 2 || size.height() > maxSide * 2))
    {
        reader.setScaledSize(size.scaled(maxSide * 2, maxSide * 2, Qt::KeepAspectRatio));
    }
    return reader.read();
}

/**
 * 把一张图压到体积上限以内：
 * 有透明的走 PNG（再大就缩边），没有透明的先按质量阶梯压 JPG，还超就缩一圈再来。
 * 返回的图不一定真的小于 maxBytes（极端情况下已经缩到很小），调用方按体积再判一次。
 */
EncodedImage compressImageToLimit(const QImage &source, qint64 maxBytes, int maxSide)
{
    QImage image = fitImage(source, maxSide);
    if (image.isNull())
        return EncodedImage();

    const bool keepAlpha = usesTransparency(image);
    EncodedImage result;
    for (int round = 0; round < 10; ++round)
    {
        if (keepAlpha)
        {
            const QByteArray png = encodePngBytes(image);
            if (png.isEmpty())
                break;
            result = {png, QStringLiteral("image/png")};
            if (result.data.size() <= maxBytes)
                return result;
        }
        else
        {
            bool jpegOk = false;
            for (int quality : {92, 85, 78, 70, 62})
            {
                const QByteArray jpeg = encodeJpegBytes(image, quality);
                if (jpeg.isEmpty())
                    break;
                jpegOk = true;
                result = {jpeg, QStringLiteral("image/jpeg")};
                if (result.data.size() <= maxBytes)
                    return result;
            }
            if (!jpegOk)
            {
                // 个别环境没装 JPEG 编码插件，退回 PNG
                const QByteArray png = encodePngBytes(image);
                if (png.isEmpty())
                    break;
                result = {png, QStringLiteral("image/png")};
                if (result.data.size() <= maxBytes)
                    return result;
            }
        }

        // 压到极限还是超了：缩掉一圈边长再来一轮
        const QSize next(qMax(32, image.width() * 4 / 5), qMax(32, image.height() * 4 / 5));
        if (next == image.size())
            break;
        image = image.scaled(next, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return result;
}
} // namespace

ForumAccountService *ForumAccountService::instance()
{
    static ForumAccountService *s_instance = new ForumAccountService();
    return s_instance;
}

ForumAccountService::ForumAccountService(QObject *parent) : QObject(parent)
{
    m_settings = new QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"), this);
    m_network = new QNetworkAccessManager(this);
    m_oauthTimer = new QTimer(this);
    m_oauthTimer->setSingleShot(true);
    connect(m_oauthTimer, &QTimer::timeout, this, &ForumAccountService::pollOAuthStatus);

    // 扫码登录：电脑端每隔一会儿问一次「手机上确认了没」
    m_qrTimer = new QTimer(this);
    m_qrTimer->setSingleShot(true);
    connect(m_qrTimer, &QTimer::timeout, this, &ForumAccountService::pollQrLogin);

    m_token = m_settings->value(QString::fromLatin1(kTokenKey)).toString();
    m_tokenBaseUrl = m_settings->value(QString::fromLatin1(kTokenBaseUrlKey)).toString();
    loadCachedUser();
    loadHumanCheck();

    // 论坛地址换了：旧令牌属于另一个站点，直接丢弃
    if (!m_token.isEmpty() && m_tokenBaseUrl != baseUrl())
    {
        clearSession();
    }

    // 运行中改了论坛地址，同样要让本地登录状态跟着失效，免得拿 A 站令牌请求 B 站
    connect(SettingsManager::instance(), &SettingsManager::forumBaseUrlChanged, this,
            [this](const QString &) {
                // 人机验证令牌也是绑站点签发的，换了论坛地址一律作废
                if (!m_humanToken.isEmpty() && m_humanTokenBaseUrl != baseUrl())
                    clearHumanCheck();
                if (m_token.isEmpty() || m_tokenBaseUrl == baseUrl())
                    return;
                clearSession();
                emit userChanged(m_user);
            });}

ForumAccountService::~ForumAccountService() = default;

bool ForumAccountService::hasSession() const
{
    return !m_token.isEmpty() && m_tokenBaseUrl == baseUrl();
}

bool ForumAccountService::isLoggedIn() const
{
    return hasSession() && m_user.isValid();
}

QString ForumAccountService::playerName() const
{
    if (!isLoggedIn())
        return QString();
    const QString display = m_user.displayName.trimmed();
    if (!display.isEmpty())
        return display;
    return m_user.username.trimmed();
}

QString ForumAccountService::baseUrl() const
{
    return SettingsManager::instance()->forumBaseUrl();
}

QString ForumAccountService::absoluteUrl(const QString &path) const
{
    if (path.isEmpty())
        return baseUrl();
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://")))
        return path;
    if (path.startsWith(QLatin1Char('/')))
        return baseUrl() + path;
    return baseUrl() + QLatin1Char('/') + path;
}

// ────────────────────────────────────────────────────────────── 账号操作

void ForumAccountService::login(const QString &username, const QString &password)
{
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    postAuth(QStringLiteral("/api/auth/login"), body);
}

void ForumAccountService::registerAccount(const QString &username, const QString &password,
                                          const QString &email, const QString &code)
{
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    // 论坛要求邮箱时这两项都要有；老论坛或管理员关掉邮箱要求时可以不填，
    // 空着就不发，免得旧服务端因为多出来的字段报参数错误
    if (!email.trimmed().isEmpty())
        body.insert(QStringLiteral("email"), email.trimmed());
    if (!code.trimmed().isEmpty())
        body.insert(QStringLiteral("code"), code.trimmed());
    postAuth(QStringLiteral("/api/auth/register"), body);
}

void ForumAccountService::fetchRegisterPolicy()
{
    // 只读接口，没登录也放行（也不经过人机验证），拿到的就是论坛现在的注册规矩
    getJson(
        QStringLiteral("/api/auth/register/policy"),
        [this](const QJsonObject &root) {
            const bool emailRequired = root.value(QStringLiteral("email_required")).toBool(true);
            const bool registerOpen = root.value(QStringLiteral("register_open")).toBool(true);
            emit registerPolicyChanged(emailRequired, registerOpen);
        },
        // 拿不到就按默认（要邮箱验证）走，不打扰用户
        [](const QString &, int) {});
}

void ForumAccountService::postAuth(const QString &path, const QJsonObject &body,
                                   const QString &successMessage)
{
    sendAuth(path, body, 1, successMessage);
}

void ForumAccountService::sendAuth(const QString &path, const QJsonObject &body, int attempt,
                                   const QString &successMessage)
{
    QNetworkRequest request{QUrl(absoluteUrl(path))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // 登录 / 注册不该带着上一次那个可能已经失效的会话；重试时还会退回 HTTP/1.1
    prepareRequest(request, false, attempt > 1);

    QNetworkReply *reply =
        m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, path, body, attempt, successMessage]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const int networkError = int(reply->error());
        const QString networkDetail = reply->errorString();
        const QString newToken = status > 0 ? cookieToken(reply) : QString();
        reply->deleteLater();

        if (status == 0)
        {
            if (attempt < kMaxAttempts && isTransientNetworkError(networkError))
            {
                qWarning() << "[ForumAccount] 登录请求失败，准备第" << (attempt + 1)
                           << "次尝试:" << networkDetail;
                QTimer::singleShot(retryDelayMs(attempt), this,
                                   [this, path, body, attempt, successMessage]() {
                    m_network->clearConnectionCache(); // 别复用那条已经挂住的连接
                    sendAuth(path, body, attempt + 1, successMessage);
                });
                return;
            }

            qWarning() << "[ForumAccount] 连不上论坛:" << absoluteUrl(path) << networkDetail;
            emit requestFailed(networkFailureMessage(networkError, networkDetail, attempt), 0);
            return;
        }
        if (status >= 400)
        {
            // 登录 / 注册同样过不了人机验证这关：先弹验证框，通过后自动重发一次
            if (status == 403 && errorCode(raw) == QLatin1String("human_required"))
            {
                // 服务端不认本地这份令牌（过期、论坛重启过、换了节点）：
                // 必须先作废，否则界面会以为已经验过、不再弹滑块，
                // 被拦下的请求就一直挂着，看着像「连不上论坛」
                clearHumanCheck();
                m_humanCheckPending = true;
                if (m_humanReplays.size() < 12)
                    m_humanReplays.append([this, path, body, successMessage]() {
                        if (m_humanToken.isEmpty())
                        {
                            emit requestFailed(tr("需要先完成人机验证，验证后再试一次"), 403);
                            return;
                        }
                        sendAuth(path, body, 1, successMessage);
                    });
                emit humanCheckRequired();
                return;
            }
            emit requestFailed(errorMessage(raw, reply), status);
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            emit requestFailed(tr("论坛返回的数据无法解析"), status);
            return;
        }

        const QJsonObject root = doc.object();
        if (newToken.isEmpty())
        {
            // 没拿到会话 Cookie 时不该假装登录成功，否则后面的请求全是未登录
            emit requestFailed(tr("登录成功但没拿到会话，检查论坛地址是否正确"), status);
            return;
        }

        saveSession(newToken);
        setUser(parseUser(root.value(QStringLiteral("user")).toObject()));

        if (!successMessage.isEmpty())
            emit requestSucceeded(successMessage);
        else if (root.value(QStringLiteral("first_user")).toBool())
            emit requestSucceeded(tr("已创建论坛第一个账号，你现在是管理员"));
        else
            emit requestSucceeded(tr("已登录论坛账号：%1")
                                      .arg(m_user.displayName.isEmpty() ? m_user.username
                                                                        : m_user.displayName));
        emit userChanged(m_user);
    });
}

void ForumAccountService::logout()
{
    if (!m_token.isEmpty())
    {
        // 尽力通知服务端销毁会话，失败也不影响本地退出
        QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/auth/logout")))};
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        prepareRequest(request);
        QNetworkReply *reply = m_network->post(request, QByteArrayLiteral("{}"));
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    }

    clearSession();
    emit requestSucceeded(tr("已退出论坛账号"));
    emit userChanged(m_user);
}

// ────────────────────────────────────────────────────────────── 第三方登录

void ForumAccountService::fetchOAuthProviders()
{
    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/auth/oauth/providers")))};
    prepareRequest(request, false);

    // 这个接口只用来决定登录卡上显示哪些按钮，失败就当作没有第三方登录，
    // 不弹错误提示、也不触发人机验证——老版本论坛没有这个接口时也应该照常用账号密码登录。
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        reply->deleteLater();

        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (status == 200 && doc.isObject())
        {
            emit oauthProvidersChanged(
                doc.object().value(QStringLiteral("providers")).toArray());
            return;
        }
        emit oauthProvidersChanged(QJsonArray());
    });
}

void ForumAccountService::startOAuthLogin(const QString &provider)
{
    if (provider.isEmpty())
        return;
    cancelOAuthLogin();
    const int generation = ++m_oauthGeneration;

    QJsonObject body;
    body.insert(QStringLiteral("provider"), provider);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/auth/oauth/device")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    prepareRequest(request, false);

    QNetworkReply *reply = m_network->post(request, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, provider, generation]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const int networkError = int(reply->error());
        const QString networkDetail = reply->errorString();
        reply->deleteLater();

        // 用户已经取消 / 重新发起过一次：这次结果直接丢掉，别在后台偷偷登录
        if (generation != m_oauthGeneration)
            return;

        if (status == 0)
        {
            emit oauthLoginFailed(networkFailureMessage(networkError, networkDetail, 1));
            return;
        }
        if (status >= 400)
        {
            emit oauthLoginFailed(errorMessage(raw, reply));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject())
        {
            emit oauthLoginFailed(tr("论坛返回的数据无法解析"));
            return;
        }

        const QJsonObject root = doc.object();
        const QString deviceCode = root.value(QStringLiteral("device_code")).toString();
        const QString verificationUrl = root.value(QStringLiteral("verification_url")).toString();
        if (deviceCode.isEmpty() || verificationUrl.isEmpty())
        {
            emit oauthLoginFailed(tr("论坛没有返回登录地址，请稍后再试"));
            return;
        }

        m_oauthProvider = provider;
        m_oauthDeviceCode = deviceCode;
        m_oauthIntervalMs = qMax(2000, root.value(QStringLiteral("interval")).toInt(3) * 1000);
        const int expiresIn = qMax(60, root.value(QStringLiteral("expires_in")).toInt(600));
        m_oauthRemainingPolls = qMax(1, (expiresIn * 1000) / m_oauthIntervalMs + 1);
        m_oauthNetworkRetries = 0;
        m_oauthBusy = true;

        emit oauthLoginStarted(provider, absoluteUrl(verificationUrl),
                               root.value(QStringLiteral("user_code")).toString(), expiresIn);
        if (m_oauthTimer)
            m_oauthTimer->start(m_oauthIntervalMs);
    });
}

void ForumAccountService::pollOAuthStatus()
{
    if (!m_oauthBusy || m_oauthDeviceCode.isEmpty())
        return;

    if (m_oauthRemainingPolls <= 0)
    {
        stopOAuthPolling();
        emit oauthLoginFailed(tr("等待浏览器授权超时了，请重新发起第三方登录"));
        return;
    }
    --m_oauthRemainingPolls;

    const int generation = m_oauthGeneration;
    // 设备码放在请求体里发：GET 查询串会被服务端访问日志原样记下来，等于把登录凭据写进日志
    QJsonObject body;
    body.insert(QStringLiteral("device_code"), m_oauthDeviceCode);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/auth/oauth/device/status")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    prepareRequest(request, false);

    QNetworkReply *reply = m_network->post(request, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const int networkError = int(reply->error());
        const QString networkDetail = reply->errorString();
        reply->deleteLater();

        if (!m_oauthBusy || generation != m_oauthGeneration)
            return; // 用户已经取消 / 关闭了等待框，或已经重新发起

        if (status == 0)
        {
            // 轮询偶尔会被网络抖动打断，别立刻报失败：给三次重试机会
            if (m_oauthNetworkRetries < 3)
            {
                ++m_oauthNetworkRetries;
                if (m_oauthTimer)
                    m_oauthTimer->start(2000);
                return;
            }
            stopOAuthPolling();
            emit oauthLoginFailed(networkFailureMessage(networkError, networkDetail, 1));
            return;
        }
        if (status >= 400)
        {
            stopOAuthPolling();
            emit oauthLoginFailed(errorMessage(raw, reply));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject())
        {
            stopOAuthPolling();
            emit oauthLoginFailed(tr("论坛返回的数据无法解析"));
            return;
        }

        const QJsonObject root = doc.object();
        const QString state = root.value(QStringLiteral("status")).toString();
        if (state == QLatin1String("pending"))
        {
            m_oauthNetworkRetries = 0;
            if (m_oauthTimer)
                m_oauthTimer->start(m_oauthIntervalMs);
            return;
        }

        if (state == QLatin1String("authorized"))
        {
            const QString sessionToken = root.value(QStringLiteral("token")).toString();
            const QJsonObject userObject = root.value(QStringLiteral("user")).toObject();
            if (sessionToken.isEmpty() || userObject.isEmpty())
            {
                stopOAuthPolling();
                emit oauthLoginFailed(tr("第三方登录成功但没拿到会话，请重试"));
                return;
            }

            stopOAuthPolling();
            saveSession(sessionToken);
            setUser(parseUser(userObject));
            emit oauthLoginSucceeded(m_user);
            emit userChanged(m_user);
            return;
        }

        const QString message = root.value(QStringLiteral("message")).toString();
        stopOAuthPolling();
        emit oauthLoginFailed(message.isEmpty() ? tr("第三方登录已失效，请重新发起") : message);
    });
}

void ForumAccountService::cancelOAuthLogin()
{
    // 递增代次：已经发出去、还没回来的设备码 / 轮询响应都作废
    ++m_oauthGeneration;
    stopOAuthPolling();
}

void ForumAccountService::stopOAuthPolling()
{
    if (m_oauthTimer)
        m_oauthTimer->stop();
    m_oauthBusy = false;
    m_oauthProvider.clear();
    m_oauthDeviceCode.clear();
    m_oauthRemainingPolls = 0;
    m_oauthNetworkRetries = 0;
}

// ────────────────────────────────────────────────────────────── 手机扫码登录

void ForumAccountService::startQrLogin(const QString &deviceName)
{
    // 连点「刷新二维码」时先把上一张作废，免得两张码同时轮询
    cancelQrLogin();
    const int generation = ++m_qrGeneration;

    QJsonObject body;
    const QString name = deviceName.trimmed();
    if (!name.isEmpty())
        body.insert(QStringLiteral("device"), name.left(80));

    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/auth/qr/create")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // 这一步发生在登录之前，手上还没有会话，别把上一次的失效令牌带上
    prepareRequest(request, false);

    QNetworkReply *reply =
        m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const int networkError = int(reply->error());
        const QString networkDetail = reply->errorString();
        reply->deleteLater();

        if (generation != m_qrGeneration)
            return; // 用户已经关掉了弹窗或重新申请过

        if (status == 0)
        {
            failQrLogin(networkFailureMessage(networkError, networkDetail, 1));
            return;
        }
        if (status >= 400)
        {
            failQrLogin(errorMessage(raw, reply));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject())
        {
            failQrLogin(tr("论坛返回的数据无法解析"));
            return;
        }

        const QJsonObject root = doc.object();
        const QString ticket = root.value(QStringLiteral("ticket")).toString();
        const QString scanUrl = root.value(QStringLiteral("scan_url")).toString();
        if (ticket.isEmpty() || scanUrl.isEmpty())
        {
            failQrLogin(tr("论坛没有返回可用的二维码，请稍后再试"));
            return;
        }

        m_qrTicket = ticket;
        m_qrScanUrl = scanUrl;
        m_qrScanned = false;
        m_qrBusy = true;
        const int expiresIn = qBound(30, root.value(QStringLiteral("expires_in")).toInt(180), 900);
        // 1.5 秒问一次：手机上一点确认，电脑端几乎是立刻登录
        m_qrRemainingPolls = qMax(1, (expiresIn * 1000) / kQrPollIntervalMs + 1);
        m_qrNetworkRetries = 0;

        emit qrLoginStarted(ticket, scanUrl, expiresIn);
        if (m_qrTimer)
            m_qrTimer->start(kQrPollIntervalMs);
    });
}

void ForumAccountService::pollQrLogin()
{
    if (!m_qrBusy || m_qrTicket.isEmpty())
        return;

    if (m_qrRemainingPolls <= 0)
    {
        stopQrPolling();
        emit qrLoginStateChanged(QStringLiteral("expired"), tr("二维码已过期，请刷新后重新扫码"));
        emit qrLoginFailed(tr("二维码已过期，请刷新后重新扫码"));
        return;
    }
    --m_qrRemainingPolls;

    const int generation = m_qrGeneration;
    // 票据放在请求体里：GET 查询串会被访问日志原样记下来，等于把登录凭据写进日志
    QJsonObject body;
    body.insert(QStringLiteral("ticket"), m_qrTicket);
    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/auth/qr/status")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    prepareRequest(request, false);

    QNetworkReply *reply =
        m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const int networkError = int(reply->error());
        const QString networkDetail = reply->errorString();
        reply->deleteLater();

        if (!m_qrBusy || generation != m_qrGeneration)
            return; // 弹窗已经关掉，或者换了一张新码

        if (status == 0)
        {
            // 轮询偶尔会被网络抖动打断，别立刻报失败：给三次重试
            if (m_qrNetworkRetries < 3)
            {
                ++m_qrNetworkRetries;
                if (m_qrTimer)
                    m_qrTimer->start(kQrPollIntervalMs);
                return;
            }
            failQrLogin(networkFailureMessage(networkError, networkDetail, 1));
            return;
        }
        if (status >= 400)
        {
            failQrLogin(errorMessage(raw, reply));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject())
        {
            failQrLogin(tr("论坛返回的数据无法解析"));
            return;
        }

        const QJsonObject root = doc.object();
        const QString state = root.value(QStringLiteral("status")).toString();
        if (state == QLatin1String("pending"))
        {
            m_qrNetworkRetries = 0;
            if (m_qrTimer)
                m_qrTimer->start(kQrPollIntervalMs);
            return;
        }

        if (state == QLatin1String("scanned"))
        {
            m_qrNetworkRetries = 0;
            // 「已扫码」只报一次，后面继续等手机确认
            if (!m_qrScanned)
            {
                m_qrScanned = true;
                emit qrLoginStateChanged(QStringLiteral("scanned"),
                                         root.value(QStringLiteral("user_name")).toString());
            }
            if (m_qrTimer)
                m_qrTimer->start(kQrPollIntervalMs);
            return;
        }

        if (state == QLatin1String("confirmed"))
        {
            const QString sessionToken = root.value(QStringLiteral("token")).toString();
            const QJsonObject userObject = root.value(QStringLiteral("user")).toObject();
            if (sessionToken.isEmpty() || userObject.isEmpty())
            {
                failQrLogin(tr("手机已确认，但没拿到会话，请重试"));
                return;
            }

            stopQrPolling();
            saveSession(sessionToken);
            setUser(parseUser(userObject));
            emit qrLoginSucceeded(m_user);
            emit userChanged(m_user);
            return;
        }

        // rejected / expired 以及别的没见过的状态：都当作这次扫码登录失败
        const QString message = root.value(QStringLiteral("message")).toString();
        const bool expired = state == QLatin1String("expired");
        stopQrPolling();
        emit qrLoginStateChanged(expired ? QStringLiteral("expired") : state,
                                 message.isEmpty() ? QString() : message);
        emit qrLoginFailed(message.isEmpty() ? tr("扫码登录已失效，请刷新二维码后重试") : message);
    });
}

void ForumAccountService::cancelQrLogin()
{
    // 递增代次：已经发出去、还没回来的创建 / 轮询响应都作废
    ++m_qrGeneration;
    stopQrPolling();
}

void ForumAccountService::stopQrPolling()
{
    if (m_qrTimer)
        m_qrTimer->stop();
    m_qrBusy = false;
    m_qrTicket.clear();
    m_qrScanUrl.clear();
    m_qrRemainingPolls = 0;
    m_qrNetworkRetries = 0;
    m_qrScanned = false;
}

void ForumAccountService::failQrLogin(const QString &message)
{
    stopQrPolling();
    emit qrLoginFailed(message);
}

void ForumAccountService::scanQrTicket(const QString &ticket)
{
    const QString normalized = ticket.trimmed();
    if (normalized.isEmpty())
    {
        emit qrTicketFailed(tr("没有读到登录码，请重新扫描"));
        return;
    }
    // 扫码登录是把手机上的登录状态授权给电脑，手机上必须自己是登录着的
    if (!isLoggedIn())
    {
        emit qrTicketFailed(tr("请先在方块盒子里登录账号，再扫电脑上的二维码"));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("ticket"), normalized);
    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/auth/qr/scan")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    prepareRequest(request);

    QNetworkReply *reply =
        m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, normalized]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const int networkError = int(reply->error());
        const QString networkDetail = reply->errorString();
        reply->deleteLater();

        if (status == 0)
        {
            emit qrTicketFailed(networkFailureMessage(networkError, networkDetail, 1));
            return;
        }
        if (status >= 400)
        {
            emit qrTicketFailed(errorMessage(raw, reply));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject())
        {
            emit qrTicketFailed(tr("论坛返回的数据无法解析"));
            return;
        }

        const QJsonObject root = doc.object();
        QJsonObject info;
        info.insert(QStringLiteral("device"), root.value(QStringLiteral("device")).toString());
        info.insert(QStringLiteral("ip"), root.value(QStringLiteral("ip")).toString());
        info.insert(QStringLiteral("expires_in"), root.value(QStringLiteral("expires_in")).toInt());
        emit qrTicketScanned(normalized, info);
    });
}

void ForumAccountService::answerQrTicket(const QString &ticket, bool approve)
{
    const QString normalized = ticket.trimmed();
    if (normalized.isEmpty())
    {
        emit qrTicketFailed(tr("没有读到登录码，请重新扫描"));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("ticket"), normalized);
    body.insert(QStringLiteral("approve"), approve);
    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/auth/qr/confirm")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    prepareRequest(request);

    QNetworkReply *reply =
        m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, normalized, approve]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const int networkError = int(reply->error());
        const QString networkDetail = reply->errorString();
        reply->deleteLater();

        if (status == 0)
        {
            emit qrTicketFailed(networkFailureMessage(networkError, networkDetail, 1));
            return;
        }
        if (status >= 400)
        {
            emit qrTicketFailed(errorMessage(raw, reply));
            return;
        }
        emit qrTicketAnswered(normalized, approve);
    });
}

void ForumAccountService::refreshProfile()
{
    if (!hasSession())
    {
        return;
    }

    getJson(
        QStringLiteral("/api/me"),
        [this](const QJsonObject &root) {
            const QJsonObject userObject = root.value(QStringLiteral("user")).toObject();
            if (userObject.isEmpty())
                return;
            setUser(parseUser(userObject));

            // 未读通知数（通知页签与角标用）
            const int unread = root.value(QStringLiteral("unread")).toInt();
            if (unread != m_unreadCount)
            {
                m_unreadCount = unread;
                emit unreadChanged(unread);
            }
            emit userChanged(m_user);
        },
        [this](const QString &message, int status) {
            if (status == 401)
            {
                emit sessionExpired();
                emit userChanged(m_user);
                return;
            }
            emit requestFailed(message, status);
        });
}

void ForumAccountService::updateProfile(const QString &displayName, const QString &bio,
                                        const QString &email)
{
    QJsonObject body;
    body.insert(QStringLiteral("display_name"), displayName);
    body.insert(QStringLiteral("bio"), bio);
    body.insert(QStringLiteral("email"), email);

    sendJson(
        QByteArrayLiteral("PATCH"), QStringLiteral("/api/me"), body,
        [this](const QJsonObject &root) {
            const QJsonObject userObject = root.value(QStringLiteral("user")).toObject();
            if (!userObject.isEmpty())
                setUser(parseUser(userObject));
            emit requestSucceeded(tr("个人资料已保存"));
            emit userChanged(m_user);
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

void ForumAccountService::sendEmailCode(const QString &email)
{
    QJsonObject body;
    body.insert(QStringLiteral("email"), email);

    sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/me/email/code"), body,
        [this](const QJsonObject &root) {
            emit emailCodeSent(root.value(QStringLiteral("expires_in")).toInt(600));
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

void ForumAccountService::sendRegisterEmailCode(const QString &email)
{
    QJsonObject body;
    body.insert(QStringLiteral("email"), email);

    sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/auth/register/code"), body,
        [this](const QJsonObject &root) {
            emit registerEmailCodeSent(root.value(QStringLiteral("expires_in")).toInt(600));
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

void ForumAccountService::sendRecoverEmailCode(const QString &email)
{
    QJsonObject body;
    body.insert(QStringLiteral("email"), email);

    sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/auth/recover/code"), body,
        [this](const QJsonObject &root) {
            emit recoverEmailCodeSent(root.value(QStringLiteral("expires_in")).toInt(600),
                                      root.value(QStringLiteral("username")).toString());
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

void ForumAccountService::recoverAccount(const QString &email, const QString &code,
                                         const QString &newPassword)
{
    QJsonObject body;
    body.insert(QStringLiteral("email"), email);
    body.insert(QStringLiteral("code"), code);
    body.insert(QStringLiteral("password"), newPassword);

    // 重设成功论坛会直接下发新会话：这里按登录成功处理，界面收到 userChanged 就回到账户总览
    postAuth(QStringLiteral("/api/auth/recover/reset"), body,
             tr("密码已重设，已登录方块盒子账号"));
}

void ForumAccountService::verifyEmailCode(const QString &email, const QString &code)
{
    QJsonObject body;
    body.insert(QStringLiteral("email"), email);
    body.insert(QStringLiteral("code"), code);

    sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/me/email/verify"), body,
        [this](const QJsonObject &root) {
            const QJsonObject userObject = root.value(QStringLiteral("user")).toObject();
            if (!userObject.isEmpty())
                setUser(parseUser(userObject));
            emit requestSucceeded(tr("邮箱验证成功"));
            emit userChanged(m_user);
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

void ForumAccountService::changePassword(const QString &currentPassword,
                                         const QString &newPassword)
{
    QJsonObject body;
    body.insert(QStringLiteral("current_password"), currentPassword);
    body.insert(QStringLiteral("new_password"), newPassword);

    const bool firstPassword = isLoggedIn() && !m_user.passwordSet;
    sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/me/password"), body,
        [this, firstPassword](const QJsonObject &) {
            if (firstPassword && isLoggedIn())
            {
                m_user.passwordSet = true;
                setUser(m_user);
                emit userChanged(m_user);
            }
            emit requestSucceeded(firstPassword ? tr("密码已设置，以后也能用账号密码登录")
                                                : tr("密码已修改，其它设备需重新登录"));
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

void ForumAccountService::uploadAvatar(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile() || info.size() <= 0)
    {
        emit requestFailed(tr("打不开这张图片，换个文件试试"), 0);
        return;
    }
    if (info.size() > kMaxSourceBytes)
    {
        emit requestFailed(
            tr("这张图片有 %1 MB，太大了，换一张小一点的吧")
                .arg(QString::number(static_cast<double>(info.size()) / (1024.0 * 1024.0), 'f', 1)),
            0);
        return;
    }

    const QByteArray format = QImageReader(filePath).format().toLower();
    QByteArray data;
    QString mime;
    bool compressed = false;

    // 没超 512 KB 又是论坛收的格式：原样发，动图头像不会被重新编码成一张静图
    if (info.size() <= kMaxAvatarBytes && isForumImageFormat(format))
    {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly))
        {
            data = file.readAll();
            file.close();
        }
        mime = mimeForFormat(format);
    }

    // 超限（或格式论坛不收）：本地先缩到 256 像素、压到 512 KB 以内再发
    if (data.isEmpty())
    {
        const QImage source = readImageForUpload(filePath, kAvatarSide);
        if (source.isNull())
        {
            emit requestFailed(tr("打不开这张图片，换个文件试试"), 0);
            return;
        }
        const EncodedImage encoded = compressImageToLimit(source, kMaxAvatarBytes, kAvatarSide);
        if (!encoded.isValid())
        {
            emit requestFailed(tr("这张图片处理失败，换一张试试"), 0);
            return;
        }
        data = encoded.data;
        mime = encoded.mime;
        compressed = true;
    }

    if (data.size() > kMaxAvatarBytes)
    {
        emit requestFailed(tr("这张图片压不进 512 KB，换一张试试"), 0);
        return;
    }

    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/me/avatar")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, mime);
    prepareRequest(request);

    QNetworkReply *reply = m_network->post(request, data);
    handleReply(
        reply,
        [this, compressed](const QJsonObject &root) {
            m_user.avatar = root.value(QStringLiteral("avatar")).toString();
            setUser(m_user);
            emit requestSucceeded(compressed ? tr("头像已更新（原图较大，已自动压缩）")
                                             : tr("头像已更新"));
            emit userChanged(m_user);
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

void ForumAccountService::removeAvatar()
{
    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/me/avatar")))};
    prepareRequest(request);

    QNetworkReply *reply = m_network->deleteResource(request);
    handleReply(
        reply,
        [this](const QJsonObject &) {
            m_user.avatar.clear();
            setUser(m_user);
            emit requestSucceeded(tr("头像已删除"));
            emit userChanged(m_user);
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

void ForumAccountService::uploadCover(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile() || info.size() <= 0)
    {
        emit requestFailed(tr("打不开这张图片，换个文件试试"), 0);
        return;
    }
    if (info.size() > kMaxSourceBytes)
    {
        emit requestFailed(
            tr("这张图片有 %1 MB，太大了，换一张小一点的吧")
                .arg(QString::number(static_cast<double>(info.size()) / (1024.0 * 1024.0), 'f', 1)),
            0);
        return;
    }

    // 按目标尺寸的两倍解码就够裁了，省得几千万像素的原图把内存吃满
    const QImage source = readImageForUpload(filePath, kCoverWidth);
    if (source.isNull())
    {
        emit requestFailed(tr("打不开这张图片，换个文件试试"), 0);
        return;
    }

    // 居中裁成 3:1 再缩到 1440×480：体积小、网页版和启动器显示一致
    const qreal targetRatio = static_cast<qreal>(kCoverWidth) / kCoverHeight;
    const qreal sourceRatio = static_cast<qreal>(source.width()) / qMax(1, source.height());
    QRect crop;
    if (sourceRatio > targetRatio)
    {
        const int width = qMax(1, qRound(source.height() * targetRatio));
        crop = QRect((source.width() - width) / 2, 0, width, source.height());
    }
    else
    {
        const int height = qMax(1, qRound(source.width() / targetRatio));
        crop = QRect(0, (source.height() - height) / 2, source.width(), height);
    }

    const QImage cover = source.copy(crop).scaled(kCoverWidth, kCoverHeight, Qt::IgnoreAspectRatio,
                                                  Qt::SmoothTransformation);
    if (cover.isNull())
    {
        emit requestFailed(tr("这张图片处理失败，换一张试试"), 0);
        return;
    }

    // 正常情况 1440×480 的 JPG 只有两三百 KB；真超了 3 MB 就一路降质量、缩边长，不再让用户自己压
    const EncodedImage encoded = compressImageToLimit(cover, kMaxCoverBytes, kCoverWidth);
    if (!encoded.isValid())
    {
        emit requestFailed(tr("这张图片处理失败，换一张试试"), 0);
        return;
    }
    if (encoded.data.size() > kMaxCoverBytes)
    {
        emit requestFailed(tr("这张封面压不进 3 MB，换一张试试"), 0);
        return;
    }

    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/me/cover")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, encoded.mime);
    prepareRequest(request);

    QNetworkReply *reply = m_network->post(request, encoded.data);
    handleReply(
        reply,
        [this, cover](const QJsonObject &root) {
            const QString previous = m_user.cover;
            m_user.cover = root.value(QStringLiteral("cover")).toString();
            // 刚上传的这张直接写进本机缓存：横幅马上就有图，不用再从服务器下一次
            CoverImageCache::save(absoluteUrl(m_user.cover), cover);
            if (!previous.isEmpty() && previous != m_user.cover)
                CoverImageCache::remove(absoluteUrl(previous));
            setUser(m_user);
            emit requestSucceeded(tr("封面已更新"));
            emit userChanged(m_user);
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

void ForumAccountService::removeCover()
{
    QNetworkRequest request{QUrl(absoluteUrl(QStringLiteral("/api/me/cover")))};
    prepareRequest(request);

    QNetworkReply *reply = m_network->deleteResource(request);
    handleReply(
        reply,
        [this](const QJsonObject &) {
            const QString previous = m_user.cover;
            m_user.cover.clear();
            if (!previous.isEmpty())
                CoverImageCache::remove(absoluteUrl(previous));
            setUser(m_user);
            emit requestSucceeded(tr("封面已删除"));
            emit userChanged(m_user);
        },
        [this](const QString &message, int status) { emit requestFailed(message, status); });
}

// ────────────────────────────────────────────────────────────── 通用请求

void ForumAccountService::getJson(const QString &path, JsonHandler onOk, FailHandler onFail)
{
    const RequestFactory send = [this, path](int attempt) -> QNetworkReply * {
        QNetworkRequest request{QUrl(absoluteUrl(path))};
        prepareRequest(request, true, attempt > 1);
        return m_network->get(request);
    };

    handleReply(send(1), std::move(onOk), std::move(onFail), send, 1);
}

void ForumAccountService::sendJson(const QByteArray &method, const QString &path,
                                   const QJsonObject &body, JsonHandler onOk, FailHandler onFail)
{
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const RequestFactory send = [this, method, path, payload](int attempt) -> QNetworkReply * {
        QNetworkRequest request{QUrl(absoluteUrl(path))};
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        prepareRequest(request, true, attempt > 1);
        return m_network->sendCustomRequest(request, method, payload);
    };

    handleReply(send(1), std::move(onOk), std::move(onFail), send, 1);
}

void ForumAccountService::prepareRequest(QNetworkRequest &request, bool withSession,
                                         bool forceHttp1) const
{
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
    request.setRawHeader("Accept", "application/json");
    // 会话 Cookie 一律由下面手工拼，不让 Qt 自带的 Cookie 罐子插手：
    // 罐子只要收到过一次 Set-Cookie（人机验证的 bbf_hv、账号密码登录的 bbf_sid）
    // 就会在之后的每个请求里用它自己的 Cookie 覆盖掉这里设置的 Cookie 头。
    // 第三方登录的会话是接口以 JSON 发回来的、罐子里根本没有，被覆盖后紧接着的
    // /api/me 就是 401 —— 于是表现为「刚提示登录成功，马上又说登录已失效」。
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    // 重试时不再走 HTTP/2：个别网络 / 反向代理会把 HTTP/2 连接掐断，退回 1.1 更容易成功
    if (forceHttp1)
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    if (withSession && !m_token.isEmpty())
    {
        request.setRawHeader("Cookie",
                             QStringLiteral("%1=%2")
                                 .arg(QString::fromLatin1(kSessionCookieName), m_token)
                                 .toUtf8());
    }
    // 论坛开了人机验证时，所有接口都要这个令牌；没有就等着被 403 拦一次
    if (!m_humanToken.isEmpty())
        request.setRawHeader(kHumanHeaderName, m_humanToken.toUtf8());
}

void ForumAccountService::handleReply(QNetworkReply *reply, JsonHandler onOk, FailHandler onFail,
                                      const RequestFactory &retry, int attempt)
{
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, onOk, onFail, retry, attempt]() {
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray raw = reply->readAll();
                const int networkError = int(reply->error());
                const QString networkDetail = reply->errorString();
                reply->deleteLater();

                if (status == 0)
                {
                    if (retry && attempt < kMaxAttempts && isTransientNetworkError(networkError))
                    {
                        qWarning() << "[ForumAccount] 请求失败，准备第" << (attempt + 1)
                                   << "次尝试:" << networkDetail;
                        scheduleRetry(retry, onOk, onFail, attempt);
                        return;
                    }
                    qWarning() << "[ForumAccount] 连不上论坛:" << networkError << networkDetail;
                    if (onFail)
                        onFail(networkFailureMessage(networkError, networkDetail, attempt), 0);
                    return;
                }

                if (status >= 400)
                {
                    // 被人机验证拦下：不发 onFail（界面弹出验证框就是回应），
                    // 顺手把这次请求记下来，验证通过后原样重发
                    if (status == 403 && errorCode(raw) == QLatin1String("human_required"))
                    {
                        // 服务端明确说不认这个令牌，就照它说的办：本地那份作废，
                        // 别再用「本地还在有效期内」当理由跳过滑块
                        clearHumanCheck();
                        m_humanCheckPending = true;
                        if (retry && m_humanReplays.size() < 12)
                        {
                            m_humanReplays.append([this, retry, onOk, onFail]() {
                                if (m_humanToken.isEmpty())
                                {
                                    if (onFail)
                                        onFail(tr("需要先完成人机验证，验证后再点刷新"), 403);
                                    return;
                                }
                                QNetworkReply *again = retry(1);
                                if (again)
                                    handleReply(again, onOk, onFail, retry, 1);
                            });
                        }
                        emit humanCheckRequired();
                        return;
                    }
                    if (status == 401 && !m_token.isEmpty())
                    {
                        // 令牌过期或被管理员踢下线：本地会话一起作废，界面回到未登录
                        clearSession();
                        emit sessionExpired();
                        emit userChanged(m_user);
                    }
                    if (onFail)
                        onFail(errorMessage(raw, reply), status);
                    return;
                }

                QJsonParseError parseError{};
                const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isObject())
                {
                    if (onFail)
                        onFail(tr("论坛返回的数据无法解析"), status);
                    return;
                }

                if (onOk)
                    onOk(doc.object());
            });
}

void ForumAccountService::scheduleRetry(const RequestFactory &retry, JsonHandler onOk,
                                        FailHandler onFail, int attempt)
{
    QTimer::singleShot(retryDelayMs(attempt), this, [this, retry, onOk, onFail, attempt]() {
        if (!retry)
            return;

        m_network->clearConnectionCache(); // 别复用那条已经挂住的连接

        QNetworkReply *next = retry(attempt + 1);
        if (!next)
        {
            if (onFail)
                onFail(tr("连不上论坛：网络请求没能发出去"), 0);
            return;
        }
        handleReply(next, onOk, onFail, retry, attempt + 1);
    });
}

bool ForumAccountService::isTransientNetworkError(int networkError)
{
    switch (static_cast<QNetworkReply::NetworkError>(networkError))
    {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::SslHandshakeFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProtocolFailure:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        return false;
    }
}

QString ForumAccountService::networkFailureMessage(int networkError, const QString &detail,
                                                   int attempts) const
{
    QString reason;
    switch (static_cast<QNetworkReply::NetworkError>(networkError))
    {
    case QNetworkReply::HostNotFoundError:
        reason = tr("找不到论坛服务器，检查一下网络连接再试");
        break;
    case QNetworkReply::ConnectionRefusedError:
        reason = tr("论坛服务器拒绝了连接，可能正在重启，过一会儿再点一次");
        break;
    case QNetworkReply::SslHandshakeFailedError:
        reason = tr("和论坛的加密连接没能建立，可能是网络被拦截，也可能是电脑时间不准");
        break;
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        reason = tr("论坛一直没回应，网络有点慢");
        break;
    case QNetworkReply::RemoteHostClosedError:
        reason = tr("论坛那边把连接提前断开了");
        break;
    default:
        reason = detail.isEmpty() ? tr("网络请求失败") : detail;
        break;
    }

    const QString message = attempts > 1
                                ? tr("连不上论坛（已自动重试 %1 次）：%2")
                                      .arg(attempts - 1)
                                      .arg(reason)
                                : tr("连不上论坛：%1").arg(reason);
    // 带上实际请求的地址，地址填错时一眼就能看出来
    return message + QLatin1Char('\n') + tr("论坛地址：%1").arg(baseUrl());
}


// ────────────────────────────────────────────────────────────── 工具

QString ForumAccountService::cookieToken(QNetworkReply *reply)
{
    if (!reply)
        return QString();

    const auto pairs = reply->rawHeaderPairs();
    for (const auto &pair : pairs)
    {
        if (pair.first.compare(QByteArrayLiteral("set-cookie"), Qt::CaseInsensitive) != 0)
            continue;

        const QByteArray cookie = pair.second;
        const QByteArray prefix = QByteArray(kSessionCookieName) + '=';
        const int start = cookie.indexOf(prefix);
        if (start < 0)
            continue;

        int end = cookie.indexOf(';', start);
        if (end < 0)
            end = cookie.size();
        return QString::fromUtf8(cookie.mid(start + prefix.size(), end - start - prefix.size()))
            .trimmed();
    }
    return QString();
}

QString ForumAccountService::errorMessage(const QByteArray &raw, QNetworkReply *reply)
{
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isObject())
    {
        const QString message = doc.object().value(QStringLiteral("error")).toString().trimmed();
        if (!message.isEmpty())
            return message;
    }

    if (reply && reply->error() != QNetworkReply::NoError)
        return reply->errorString();
    return tr("论坛返回了一个错误，稍后再试");
}

ForumUser ForumAccountService::parseUser(const QJsonObject &object)
{
    ForumUser user;
    user.id = object.value(QStringLiteral("id")).toInt();
    user.username = object.value(QStringLiteral("username")).toString();
    user.displayName = object.value(QStringLiteral("display_name")).toString();
    if (user.displayName.isEmpty())
        user.displayName = user.username;
    user.bio = object.value(QStringLiteral("bio")).toString();
    user.avatar = object.value(QStringLiteral("avatar")).toString();
    user.cover = object.value(QStringLiteral("cover")).toString();
    user.role = object.value(QStringLiteral("role")).toString();
    user.status = object.value(QStringLiteral("status")).toString();
    user.threadCount = object.value(QStringLiteral("thread_count")).toInt();
    user.postCount = object.value(QStringLiteral("post_count")).toInt();
    user.points = object.value(QStringLiteral("points")).toInt();
    // 会员身份：老版本论坛没有这个字段，缺省就是「没有会员」
    const QJsonObject member = object.value(QStringLiteral("member")).toObject();
    if (member.value(QStringLiteral("active")).toBool())
    {
        user.memberTier = member.value(QStringLiteral("tier")).toString();
        user.memberBadge = member.value(QStringLiteral("badge")).toString();
        if (user.memberBadge.isEmpty())
            user.memberBadge = member.value(QStringLiteral("name")).toString();
        user.memberColor = member.value(QStringLiteral("color")).toString();
        user.memberUntil = static_cast<qint64>(member.value(QStringLiteral("until")).toDouble());
        user.memberPermanent = member.value(QStringLiteral("permanent")).toBool();
    }
    user.quizPassed = object.value(QStringLiteral("quiz_passed")).toBool();
    user.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
    user.email = object.value(QStringLiteral("email")).toString();
    user.emailVerified = object.value(QStringLiteral("email_verified")).toBool();
    // 老版本论坛不返回这个字段：缺省按「已设置密码」处理，行为与以前一致
    user.passwordSet = object.value(QStringLiteral("password_set")).toBool(true);
    return user;
}

void ForumAccountService::setUser(const ForumUser &user, bool cache)
{
    m_user = user;
    if (cache && m_settings)
    {
        const QJsonObject object{
            {QStringLiteral("id"), m_user.id},
            {QStringLiteral("username"), m_user.username},
            {QStringLiteral("display_name"), m_user.displayName},
            {QStringLiteral("bio"), m_user.bio},
            {QStringLiteral("avatar"), m_user.avatar},
            {QStringLiteral("cover"), m_user.cover},
            {QStringLiteral("role"), m_user.role},
            {QStringLiteral("status"), m_user.status},
            {QStringLiteral("thread_count"), m_user.threadCount},
            {QStringLiteral("post_count"), m_user.postCount},
            {QStringLiteral("points"), m_user.points},
            {QStringLiteral("member"),
             m_user.memberTier.isEmpty()
                 ? QJsonObject()
                 : QJsonObject{{QStringLiteral("active"), true},
                               {QStringLiteral("tier"), m_user.memberTier},
                               {QStringLiteral("name"), m_user.memberBadge},
                               {QStringLiteral("badge"), m_user.memberBadge},
                               {QStringLiteral("color"), m_user.memberColor},
                               {QStringLiteral("until"), static_cast<double>(m_user.memberUntil)},
                               {QStringLiteral("permanent"), m_user.memberPermanent}}},
            {QStringLiteral("quiz_passed"), m_user.quizPassed},
            {QStringLiteral("created_at"), static_cast<double>(m_user.createdAt)},
            {QStringLiteral("email"), m_user.email},
            {QStringLiteral("email_verified"), m_user.emailVerified},
            {QStringLiteral("password_set"), m_user.passwordSet},
        };
        m_settings->setValue(QString::fromLatin1(kUserCacheKey),
                             QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    }

    // 老玩家迁移：本机早就通过过答题、但这个账号还没有记录的，趁拉到资料补报一次。
    // 成功后记 quiz/syncedToAccount，只补这一次，避免换账号登录时把本机成绩带过去。
    if (cache && user.isValid() && !user.quizPassed && QuizBank::hasPassed() && m_settings
        && !m_settings->value(QString::fromLatin1(kQuizSyncedKey)).toBool())
    {
        reportQuizPass(QuizBank::lastScore());
    }
}

void ForumAccountService::reportQuizPass(int score)
{
    if (!hasSession())
        return;

    QJsonObject body;
    body.insert(QStringLiteral("score"), score);
    sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/quiz/complete"), body,
        [this](const QJsonObject &root) {
            if (!root.value(QStringLiteral("quiz_passed")).toBool())
                return;
            if (m_settings)
                m_settings->setValue(QString::fromLatin1(kQuizSyncedKey), true);
            if (m_user.isValid() && !m_user.quizPassed)
            {
                m_user.quizPassed = true;
                setUser(m_user);
                emit userChanged(m_user);
            }
        },
        [this](const QString &message, int status) {
            qWarning() << "[ForumAccount] 答题成绩上报失败:" << message << status;
        });
}

bool ForumAccountService::hasPassedQuiz() const
{
    if (m_user.isValid() && m_user.quizPassed)
        return true;
    return QuizBank::hasPassed();
}

void ForumAccountService::saveSession(const QString &token)
{
    m_token = token;
    m_tokenBaseUrl = baseUrl();
    if (m_settings)
    {
        m_settings->setValue(QString::fromLatin1(kTokenKey), m_token);
        m_settings->setValue(QString::fromLatin1(kTokenBaseUrlKey), m_tokenBaseUrl);
        m_settings->sync();
    }
}

void ForumAccountService::clearSession()
{
    m_token.clear();
    m_tokenBaseUrl.clear();
    m_user = ForumUser();
    // 退出登录后本机不再留着这个账号的封面图
    CoverImageCache::clear();
    if (m_unreadCount != 0)
    {
        m_unreadCount = 0;
        emit unreadChanged(0);
    }
    if (m_settings)
    {
        m_settings->remove(QString::fromLatin1(kTokenKey));
        m_settings->remove(QString::fromLatin1(kTokenBaseUrlKey));
        m_settings->remove(QString::fromLatin1(kUserCacheKey));
        m_settings->sync();
    }
}

void ForumAccountService::loadCachedUser()
{
    if (m_token.isEmpty() || m_tokenBaseUrl != baseUrl())
        return;

    const QString cached = m_settings->value(QString::fromLatin1(kUserCacheKey)).toString();
    if (cached.isEmpty())
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(cached.toUtf8());
    if (!doc.isObject())
        return;

    const ForumUser cachedUser = parseUser(doc.object());
    if (cachedUser.isValid())
        m_user = cachedUser;
}

// ────────────────────────────────────────────────────────────── 人机验证

bool ForumAccountService::humanCheckSatisfied() const
{
    if (m_humanToken.isEmpty())
        return false;
    if (m_humanTokenBaseUrl != baseUrl())
        return false;
    return m_humanExpiresAt > QDateTime::currentSecsSinceEpoch();
}

void ForumAccountService::requestHumanChallenge()
{
    getJson(
        QStringLiteral("/api/human/challenge"),
        [this](const QJsonObject &root) {
            if (root.value(QStringLiteral("id")).toString().isEmpty())
            {
                emit humanCheckFailed(tr("验证题没拿全，稍后再试"));
                return;
            }
            emit humanChallengeReady(root);
        },
        [this](const QString &message, int) { emit humanCheckFailed(message); });
}

void ForumAccountService::fetchHumanCheckStatus(
    std::function<void(bool enabled, bool verified, int remaining)> onOk,
    FailHandler onFail)
{
    getJson(
        QStringLiteral("/api/human/status"),
        [onOk](const QJsonObject &root) {
            if (!onOk)
                return;
            const bool enabled = root.value(QStringLiteral("enabled")).toBool(false);
            const bool verified = root.value(QStringLiteral("verified")).toBool(false);
            const int remaining = root.value(QStringLiteral("remaining")).toInt(0);
            onOk(enabled, verified, remaining);
        },
        std::move(onFail));
}

void ForumAccountService::submitHumanCheck(const QString &challengeId, int answer, int durationMs)
{
    QJsonObject body;
    body.insert(QStringLiteral("id"), challengeId);
    body.insert(QStringLiteral("answer"), answer);
    body.insert(QStringLiteral("duration"), durationMs);

    sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/human/verify"), body,
        [this](const QJsonObject &root) {
            const QString token = root.value(QStringLiteral("token")).toString();
            if (token.isEmpty())
            {
                emit humanCheckFailed(tr("验证通过了，但没拿到令牌，请重试"));
                return;
            }
            const qint64 expires = qint64(root.value(QStringLiteral("expires")).toDouble());
            saveHumanCheck(token, expires);
            m_humanCheckPending = false;
            emit humanCheckPassed();
            flushHumanCheckQueue();
        },
        [this](const QString &message, int) { emit humanCheckFailed(message); });
}

void ForumAccountService::clearHumanCheck()
{
    m_humanToken.clear();
    m_humanExpiresAt = 0;
    m_humanTokenBaseUrl.clear();
    if (m_settings)
    {
        m_settings->remove(QString::fromLatin1(kHumanTokenKey));
        m_settings->remove(QString::fromLatin1(kHumanExpiresKey));
        m_settings->remove(QString::fromLatin1(kHumanBaseUrlKey));
        m_settings->sync();
    }
}

void ForumAccountService::saveHumanCheck(const QString &token, qint64 expiresAt)
{
    m_humanToken = token;
    // 服务端没给到期时间就按 1 小时算，最坏情况只是多验一次
    m_humanExpiresAt = expiresAt > 0 ? expiresAt : QDateTime::currentSecsSinceEpoch() + 3600;
    m_humanTokenBaseUrl = baseUrl();
    if (m_settings)
    {
        m_settings->setValue(QString::fromLatin1(kHumanTokenKey), m_humanToken);
        m_settings->setValue(QString::fromLatin1(kHumanExpiresKey), m_humanExpiresAt);
        m_settings->setValue(QString::fromLatin1(kHumanBaseUrlKey), m_humanTokenBaseUrl);
        m_settings->sync();
    }
}

void ForumAccountService::loadHumanCheck()
{
    m_humanToken = m_settings->value(QString::fromLatin1(kHumanTokenKey)).toString();
    m_humanExpiresAt = m_settings->value(QString::fromLatin1(kHumanExpiresKey)).toLongLong();
    m_humanTokenBaseUrl = m_settings->value(QString::fromLatin1(kHumanBaseUrlKey)).toString();

    // 换了论坛地址、或者令牌已经过期：本地这份就别再往外发了
    if (!m_humanToken.isEmpty() && !humanCheckSatisfied())
        clearHumanCheck();
}

void ForumAccountService::flushHumanCheckQueue()
{
    if (m_humanReplays.isEmpty())
        return;

    const QVector<std::function<void()>> pending = m_humanReplays;
    m_humanReplays.clear();
    for (const auto &replay : pending)
    {
        if (replay)
            replay();
    }
}

void ForumAccountService::abortHumanCheck()
{
    m_humanCheckPending = false;
    if (m_humanReplays.isEmpty())
        return;

    // 令牌还是空的，这些重发会直接以「需要人机验证」回失败，
    // 页面就不用干等着了（否则会一直显示「正在连接论坛…」）
    const QVector<std::function<void()>> pending = m_humanReplays;
    m_humanReplays.clear();
    for (const auto &replay : pending)
    {
        if (replay)
            replay();
    }
}

QString ForumAccountService::errorCode(const QByteArray &raw)
{
    if (raw.isEmpty())
        return QString();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return QString();
    return doc.object().value(QStringLiteral("code")).toString();
}
