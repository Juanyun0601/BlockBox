/**
 * @file   PanoramaThemeManager.cpp
 * @brief  旋转全景主题素材的按需下载与本地缓存实现
 * @author BlockBox Team
 */
#include "PanoramaThemeManager.h"

#include "AppVersion.h"
#include "AssetUrls.h"
#include "../platform.h"

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kThemeFileCount = 6;
// 正常全景图都在 100KB 以上；低于这个体积基本是错误页或被截断的文件
constexpr qint64 kMinFileBytes = 4096;
// 等待连接/首包的超时：国内直连慢时也要给足时间
constexpr int kConnectTimeoutMs = 25000;
// 已经开始收数据后的「停滞」超时：只要还在收到数据就不断重置计时，避免慢速网络被误杀
constexpr int kStallTimeoutMs = 12000;

struct ThemeEntry
{
    const char *id;
    const char *name;
};

// 与设置页「全景场景」下拉保持一致（顺序也一致）
const ThemeEntry kThemes[] = {
    { "overworld",   "主世界" },
    { "nether",      "下界" },
    { "end",         "末地" },
    { "cherry",      "樱花" },
    { "desert",      "沙漠" },
    { "mountains",   "山地" },
    { "darkforest",  "黑森林" },
    { "deep_dark",   "深暗之域" },
    { "wild",        "红树林沼泽" },
    { "menu_classic", "主菜单·经典" },
    { "menu_modern", "主菜单·现代" },
};

// 数据目录在启动后就固定了，缓存一次避免每次检查文件都去探测可写路径
QString panoramaRootDir()
{
    static const QString root =
        Platform::getDataDirectory() + QStringLiteral("/panorama");
    return root;
}

} // namespace

PanoramaThemeManager* PanoramaThemeManager::m_instance = nullptr;
QMutex PanoramaThemeManager::m_instanceMutex;

PanoramaThemeManager::PanoramaThemeManager(QObject *parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
}

PanoramaThemeManager* PanoramaThemeManager::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new PanoramaThemeManager();
        }
    }
    return m_instance;
}

QStringList PanoramaThemeManager::themeIds()
{
    QStringList ids;
    for (const ThemeEntry &entry : kThemes) {
        ids.append(QString::fromLatin1(entry.id));
    }
    return ids;
}

QString PanoramaThemeManager::defaultThemeId()
{
    return QStringLiteral("overworld");
}

bool PanoramaThemeManager::isBuiltinTheme(const QString &themeId)
{
    for (const ThemeEntry &entry : kThemes) {
        if (themeId == QLatin1String(entry.id)) {
            return true;
        }
    }
    return false;
}

int PanoramaThemeManager::fileCount()
{
    return kThemeFileCount;
}

QString PanoramaThemeManager::displayName(const QString &themeId)
{
    for (const ThemeEntry &entry : kThemes) {
        if (themeId == QLatin1String(entry.id)) {
            return QString::fromUtf8(entry.name);
        }
    }
    return themeId;
}

QString PanoramaThemeManager::themeDir(const QString &themeId)
{
    if (!isBuiltinTheme(themeId)) {
        return QString();
    }
    return panoramaRootDir() + QStringLiteral("/themes/") + themeId;
}

QString PanoramaThemeManager::fileNameFor(int index)
{
    return QStringLiteral("panorama_%1.jpg").arg(index);
}

bool PanoramaThemeManager::fileLooksValid(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.size() < kMinFileBytes) {
        return false;
    }

    // 只读文件头判断是不是 JPEG，避免把错误页/半截文件当成有效缓存
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray head = file.read(2);
    return head.size() == 2
        && static_cast<unsigned char>(head.at(0)) == 0xFF
        && static_cast<unsigned char>(head.at(1)) == 0xD8;
}

bool PanoramaThemeManager::isThemeReady(const QString &themeId)
{
    const QString dir = themeDir(themeId);
    if (dir.isEmpty()) {
        return false;
    }
    for (int i = 0; i < kThemeFileCount; ++i) {
        if (!fileLooksValid(dir + QLatin1Char('/') + fileNameFor(i))) {
            return false;
        }
    }
    return true;
}

QStringList PanoramaThemeManager::sourceBaseUrls()
{
    QStringList urls;

    // 本地调试 / 私有部署：指到任意静态目录（下面直接拼 <主题>/panorama_N.jpg）
    const QString custom = qEnvironmentVariable("BLOCKBOX_PANORAMA_BASE_URL").trimmed();
    if (!custom.isEmpty()) {
        urls.append(custom);
    }

    // CDN 边缘优先（与论坛公开资源同一套 CDN）：国内访问更快，也不占源站出口带宽
    urls.append(AssetUrls::cdnBase() + QStringLiteral("/assets/panorama"));

    // CDN 没命中 / 临时不可用时回退官网直连：方块盒子官网/assets/panorama/<主题>/panorama_N.jpg
    urls.append(AssetUrls::originBase() + QStringLiteral("/assets/panorama"));

    // 素材同时放在启动器发布仓库里，再用公共 CDN / GitHub 兜底；主域名不通时换备用源
    const QString repo = QStringLiteral("%1/%2")
                             .arg(AppVersion::repoOwner(), AppVersion::repoName());
    urls.append(QStringLiteral("https://cdn.jsdelivr.net/gh/%1@main/Images/Panorama").arg(repo));
    urls.append(QStringLiteral("https://raw.githubusercontent.com/%1/main/Images/Panorama").arg(repo));
    return urls;
}

bool PanoramaThemeManager::isDownloading(const QString &themeId) const
{
    return !m_activeTheme.isEmpty() && m_activeTheme == themeId;
}

void PanoramaThemeManager::ensureTheme(const QString &themeId, bool replaceActive)
{
    if (!isBuiltinTheme(themeId)) {
        emit themeFailed(themeId, tr("未知的全景主题"));
        return;
    }

    if (isThemeReady(themeId)) {
        emit themeReady(themeId);
        return;
    }

    if (m_activeTheme == themeId) {
        return;
    }

    if (!m_activeTheme.isEmpty()) {
        // 后台自动补素材时不要打断用户正在下载的主题；只有主动选主题才替换
        if (!replaceActive) {
            return;
        }
        cancelTheme(m_activeTheme);
    }

    const QString dir = themeDir(themeId);
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        emit themeFailed(themeId, tr("无法创建缓存目录"));
        return;
    }

    m_activeTheme = themeId;
    m_fileIndex = 0;
    m_completedFiles = 0;
    m_sourceIndex = 0;
    m_currentFilePercent = 0;
    m_receivedBytes = 0;

    // 上次下到一半的主题：已完整的图片直接算完成，只补缺的那几张
    for (int i = 0; i < kThemeFileCount; ++i) {
        if (fileLooksValid(dir + QLatin1Char('/') + fileNameFor(i))) {
            ++m_completedFiles;
        }
    }

    emit themeProgress(m_activeTheme, m_completedFiles, kThemeFileCount, 0, 0);
    startNextFile();
}

void PanoramaThemeManager::startNextFile()
{
    if (m_activeTheme.isEmpty()) {
        return;
    }

    const QString dir = themeDir(m_activeTheme);
    while (m_fileIndex < kThemeFileCount) {
        const QString path = dir + QLatin1Char('/') + fileNameFor(m_fileIndex);
        if (fileLooksValid(path)) {
            ++m_fileIndex;
            continue;
        }

        m_sourceIndex = 0;
        m_currentFilePercent = 0;
        requestFile(m_fileIndex);
        return;
    }

    finishTheme();
}

void PanoramaThemeManager::requestFile(int index)
{
    if (m_activeTheme.isEmpty()) {
        return;
    }

    const QStringList bases = sourceBaseUrls();
    if (m_sourceIndex < 0 || m_sourceIndex >= bases.size()) {
        failTheme(tr("网络连接失败"));
        return;
    }

    const QString url = QStringLiteral("%1/%2/%3")
                            .arg(bases.at(m_sourceIndex), m_activeTheme, fileNameFor(index));
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // 固定素材，命中 Qt 的 HTTP 缓存可能拿到旧图
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("User-Agent",
                         QStringLiteral("BlockBox/%1").arg(AppVersion::current()).toUtf8());
    request.setRawHeader("Accept", "image/jpeg");

    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;

    m_timeout = new QTimer(reply);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    m_timeout->start(kConnectTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply != m_reply) {
            return;
        }
        handleReplyFinished();
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 received, qint64 total) {
        if (reply != m_reply) {
            return;
        }
        handleDownloadProgress(received, total);
    });
}

void PanoramaThemeManager::handleDownloadProgress(qint64 received, qint64 total)
{
    if (m_activeTheme.isEmpty()) {
        return;
    }

    // 还在收数据就重置停滞计时：慢速网络只要在往前走，就不要切源/判失败
    if (m_timeout) {
        m_timeout->start(kStallTimeoutMs);
    }

    const int percent = total > 0
        ? int(qBound<qint64>(qint64(0), received * 100 / total, qint64(100)))
        : 0;
    if (percent == m_currentFilePercent) {
        return;
    }
    m_currentFilePercent = percent;

    emit themeProgress(m_activeTheme, m_completedFiles, kThemeFileCount,
                       m_receivedBytes + received, percent);
}

void PanoramaThemeManager::handleReplyFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply) {
        return;
    }

    if (m_timeout) {
        m_timeout->stop();
        m_timeout->deleteLater();
        m_timeout = nullptr;
    }
    m_reply = nullptr;

    if (m_activeTheme.isEmpty()) {
        reply->deleteLater();
        return;
    }

    const QString dir = themeDir(m_activeTheme);
    const QString path = dir + QLatin1Char('/') + fileNameFor(m_fileIndex);

    if (reply->error() != QNetworkReply::NoError) {
        const QString errorText = reply->errorString();
        reply->deleteLater();
        tryNextSourceOrFail(errorText);
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (status > 0 && status != 200) {
        tryNextSourceOrFail(tr("服务器返回 HTTP %1").arg(status));
        return;
    }
    if (data.size() < kMinFileBytes) {
        tryNextSourceOrFail(tr("下载到的文件不完整"));
        return;
    }

    // 真正解码一次：错误页、截断的 JPEG 都别留给后面的渲染器
    QBuffer buffer;
    buffer.setData(data);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    if (reader.read().isNull()) {
        tryNextSourceOrFail(tr("下载到的文件不是有效图片"));
        return;
    }

    // QSaveFile 先写临时文件，commit 成功才落盘：中途断网不会留下半截缓存
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(data) != data.size()
        || !file.commit()) {
        tryNextSourceOrFail(tr("保存全景图失败"));
        return;
    }

    ++m_completedFiles;
    ++m_fileIndex;
    m_receivedBytes += data.size();
    m_currentFilePercent = 100;
    emit themeProgress(m_activeTheme, m_completedFiles, kThemeFileCount,
                       m_receivedBytes, 100);
    startNextFile();
}

void PanoramaThemeManager::tryNextSourceOrFail(const QString &reason)
{
    ++m_sourceIndex;
    if (m_sourceIndex < sourceBaseUrls().size()) {
        m_currentFilePercent = 0;
        requestFile(m_fileIndex);
        return;
    }
    failTheme(reason);
}

void PanoramaThemeManager::finishTheme()
{
    const QString finishedTheme = m_activeTheme;
    if (finishedTheme.isEmpty()) {
        return;
    }

    resetActiveState();
    if (isThemeReady(finishedTheme)) {
        emit themeReady(finishedTheme);
    } else {
        emit themeFailed(finishedTheme, tr("下载完成后校验失败"));
    }
}

void PanoramaThemeManager::failTheme(const QString &reason)
{
    const QString failedTheme = m_activeTheme;
    if (failedTheme.isEmpty()) {
        return;
    }

    resetActiveState();
    emit themeFailed(failedTheme, reason.isEmpty() ? tr("网络连接失败") : reason);
}

void PanoramaThemeManager::resetActiveState()
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    if (m_timeout) {
        m_timeout->stop();
        m_timeout->deleteLater();
        m_timeout = nullptr;
    }

    m_activeTheme.clear();
    m_fileIndex = 0;
    m_completedFiles = 0;
    m_sourceIndex = 0;
    m_currentFilePercent = 0;
    m_receivedBytes = 0;
}

void PanoramaThemeManager::cancelTheme(const QString &themeId)
{
    if (m_activeTheme.isEmpty()) {
        return;
    }
    if (!themeId.isEmpty() && themeId != m_activeTheme) {
        return;
    }

    const QString cancelled = m_activeTheme;
    resetActiveState();
    qDebug() << "[PanoramaThemeManager] 已取消下载:" << cancelled;
}
