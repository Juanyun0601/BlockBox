/**
 * @file   ForumAccountService.cpp
 * @brief  方块盒子论坛账户服务实现
 * @author BlockBox Team
 * @date   2026-10-31
 */
#include "ForumAccountService.h"

#include <QBuffer>
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

#include "SettingsManager.h"
#include "QuizBank.h"

namespace
{
constexpr auto kTokenKey = "forum/sessionToken";
constexpr auto kTokenBaseUrlKey = "forum/sessionBaseUrl";
constexpr auto kUserCacheKey = "forum/cachedUser";
constexpr auto kQuizSyncedKey = "quiz/syncedToAccount";
constexpr auto kSessionCookieName = "bbf_sid";
constexpr int kRequestTimeoutMs = 15000;

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

    m_token = m_settings->value(QString::fromLatin1(kTokenKey)).toString();
    m_tokenBaseUrl = m_settings->value(QString::fromLatin1(kTokenBaseUrlKey)).toString();
    loadCachedUser();

    // 论坛地址换了：旧令牌属于另一个站点，直接丢弃
    if (!m_token.isEmpty() && m_tokenBaseUrl != baseUrl())
    {
        clearSession();
    }

    // 运行中改了论坛地址，同样要让本地登录状态跟着失效，免得拿 A 站令牌请求 B 站
    connect(SettingsManager::instance(), &SettingsManager::forumBaseUrlChanged, this,
            [this](const QString &) {
                if (m_token.isEmpty() || m_tokenBaseUrl == baseUrl())
                    return;
                clearSession();
                emit userChanged(m_user);
            });
}

ForumAccountService::~ForumAccountService() = default;

bool ForumAccountService::hasSession() const
{
    return !m_token.isEmpty() && m_tokenBaseUrl == baseUrl();
}

bool ForumAccountService::isLoggedIn() const
{
    return hasSession() && m_user.isValid();
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

void ForumAccountService::registerAccount(const QString &username, const QString &password)
{
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    postAuth(QStringLiteral("/api/auth/register"), body);
}

void ForumAccountService::postAuth(const QString &path, const QJsonObject &body)
{
    sendAuth(path, body, 1);
}

void ForumAccountService::sendAuth(const QString &path, const QJsonObject &body, int attempt)
{
    QNetworkRequest request{QUrl(absoluteUrl(path))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // 登录 / 注册不该带着上一次那个可能已经失效的会话；重试时还会退回 HTTP/1.1
    prepareRequest(request, false, attempt > 1);

    QNetworkReply *reply =
        m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply, path, body, attempt]() {
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
                QTimer::singleShot(retryDelayMs(attempt), this, [this, path, body, attempt]() {
                    m_network->clearConnectionCache(); // 别复用那条已经挂住的连接
                    sendAuth(path, body, attempt + 1);
                });
                return;
            }

            qWarning() << "[ForumAccount] 连不上论坛:" << absoluteUrl(path) << networkDetail;
            emit requestFailed(networkFailureMessage(networkError, networkDetail, attempt), 0);
            return;
        }
        if (status >= 400)
        {
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

        if (root.value(QStringLiteral("first_user")).toBool())
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

void ForumAccountService::changePassword(const QString &currentPassword,
                                         const QString &newPassword)
{
    QJsonObject body;
    body.insert(QStringLiteral("current_password"), currentPassword);
    body.insert(QStringLiteral("new_password"), newPassword);

    sendJson(
        QByteArrayLiteral("POST"), QStringLiteral("/api/me/password"), body,
        [this](const QJsonObject &) { emit requestSucceeded(tr("密码已修改，其它设备需重新登录")); },
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
        [this](const QJsonObject &root) {
            m_user.cover = root.value(QStringLiteral("cover")).toString();
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
            m_user.cover.clear();
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
        reason = tr("找不到论坛服务器，检查「论坛设置」里的地址是不是写错了");
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
    user.quizPassed = object.value(QStringLiteral("quiz_passed")).toBool();
    user.createdAt = static_cast<qint64>(object.value(QStringLiteral("created_at")).toDouble());
    user.email = object.value(QStringLiteral("email")).toString();
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
            {QStringLiteral("quiz_passed"), m_user.quizPassed},
            {QStringLiteral("created_at"), static_cast<double>(m_user.createdAt)},
            {QStringLiteral("email"), m_user.email},
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
