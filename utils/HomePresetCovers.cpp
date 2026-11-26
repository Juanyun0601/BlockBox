/**
 * @file   HomePresetCovers.cpp
 * @brief  首页预设封面图下载实现
 */

#include "utils/HomePresetCovers.h"

#include "utils/AppVersion.h"
#include "utils/AssetUrls.h"

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

/// 预设封面张数（与官网 assets/home-covers/preset_1.jpg … preset_8.jpg 对应）
constexpr int kPresetCount = 8;

/// 封面图在官网 / CDN 上的目录（域名见 utils/AssetUrls.h）
const char *const kCoverPath = "/assets/home-covers/";

/// 本机存放目录（应用数据目录下的子目录）
const char *const kCoverDirName = "home_covers";

/// 单张请求超时
constexpr int kRequestTimeoutMs = 20000;

/// 可用的取图来源：CDN 优先、官网兜底（本地调试 / 私有部署可用环境变量顶到最前）
QStringList coverBaseUrls()
{
    QStringList urls;

    const QString custom = qEnvironmentVariable("BLOCKBOX_HOME_COVER_BASE_URL").trimmed();
    if (!custom.isEmpty())
    {
        QString base = custom;
        while (base.endsWith(QLatin1Char('/')))
            base.chop(1);
        urls.append(base + QLatin1Char('/'));
    }

    urls.append(AssetUrls::baseCandidates(QString::fromLatin1(kCoverPath)));
    return urls;
}

/// 第 index 张（从 1 开始）封面在本机的文件名
QString coverFileName(int index)
{
    return QStringLiteral("preset_%1.jpg").arg(index);
}

} // namespace

// ============================================================================
// 构造 / 析构 / 单例
// ============================================================================

HomePresetCovers::HomePresetCovers(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
    , m_total(kPresetCount)
{
}

HomePresetCovers::~HomePresetCovers()
{
    abortReply();
}

HomePresetCovers *HomePresetCovers::instance()
{
    // 只由界面线程访问；刻意不释放，避免退出阶段与网络回调的析构顺序问题
    static HomePresetCovers *s_instance = new HomePresetCovers();
    return s_instance;
}

// ============================================================================
// 本机文件
// ============================================================================

int HomePresetCovers::presetCount()
{
    return kPresetCount;
}

QString HomePresetCovers::coverDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QLatin1Char('/') + QLatin1String(kCoverDirName);
}

QString HomePresetCovers::localFile(int index)
{
    return coverDir() + QLatin1Char('/') + coverFileName(index);
}

QStringList HomePresetCovers::localFiles()
{
    QStringList files;
    for (int i = 1; i <= kPresetCount; ++i)
    {
        const QString path = localFile(i);
        const QFileInfo info(path);
        if (!info.exists() || info.size() <= 0)
            return QStringList();   // 缺一张就当没下完，首页继续显示下载入口
        files.append(path);
    }
    return files;
}

bool HomePresetCovers::isDownloaded()
{
    return !localFiles().isEmpty();
}

// ============================================================================
// 下载
// ============================================================================

void HomePresetCovers::download()
{
    if (m_downloading)
        return;

    if (!QDir().mkpath(coverDir()))
    {
        finishWithError(tr("无法创建封面图存放目录，请检查磁盘空间"));
        return;
    }

    m_total = kPresetCount;
    m_nextIndex = 1;
    m_done = 0;
    m_sourceIndex = 0;
    m_lastError.clear();
    m_downloading = true;
    emit downloadingChanged(true);
    emit progressChanged(0, m_total);
    startNext();
}

void HomePresetCovers::startNext()
{
    if (!m_downloading)
        return;

    // 之前下过的（比如上次中途取消）直接跳过，只补缺的那几张
    while (m_nextIndex <= m_total && QFileInfo(localFile(m_nextIndex)).size() > 0)
    {
        ++m_done;
        ++m_nextIndex;
        emit progressChanged(m_done, m_total);
    }

    if (m_nextIndex > m_total)
    {
        finishWithSuccess();
        return;
    }

    requestFile(m_nextIndex);
}

void HomePresetCovers::requestFile(int index)
{
    if (!m_downloading)
        return;

    const QStringList bases = coverBaseUrls();
    if (m_sourceIndex < 0 || m_sourceIndex >= bases.size())
    {
        finishWithError(tr("第 %1 张封面图下载失败：网络连接失败").arg(index));
        return;
    }

    const QString url = bases.at(m_sourceIndex) + coverFileName(index);
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // 走的都是自家固定文件，不要命中 Qt 的 HTTP 缓存（否则可能拿到旧图）
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BlockBox/%1").arg(AppVersion::current()));

    QNetworkReply *reply = m_manager->get(request);
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, index]()
    {
        if (m_reply == reply)
            m_reply = nullptr;

        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkErrorText = reply->errorString();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (!m_downloading)
            return;   // 已经取消，丢弃这次回调

        if (networkError != QNetworkReply::NoError)
        {
            tryNextSource(index, networkErrorText);
            return;
        }
        if (status > 0 && status != 200)
        {
            tryNextSource(index, tr("服务器返回 HTTP %1").arg(status));
            return;
        }
        if (payload.isEmpty())
        {
            tryNextSource(index, tr("内容是空的"));
            return;
        }

        // 校验拿到的确实是一张能解码的图片，避免把错误页 / 占位内容存成 .jpg
        QBuffer buffer;
        buffer.setData(payload);
        QImageReader reader(&buffer);
        reader.setDecideFormatFromContent(true);
        if (!reader.canRead())
        {
            tryNextSource(index, tr("不是有效的图片"));
            return;
        }

        const QString path = localFile(index);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(payload) != payload.size()
            || !file.commit())
        {
            finishWithError(tr("封面图保存失败，请检查磁盘空间"));
            return;
        }

        ++m_done;
        m_nextIndex = index + 1;
        emit progressChanged(m_done, m_total);
        startNext();
    });
}

void HomePresetCovers::tryNextSource(int index, const QString &reason)
{
    const QStringList bases = coverBaseUrls();
    if (m_sourceIndex >= 0 && m_sourceIndex < bases.size())
    {
        qWarning() << "[HomePresetCovers] 第" << index << "张封面从"
                   << bases.at(m_sourceIndex) << "取图失败，换下一个来源:" << reason;
    }

    // CDN 没取到（未命中 / 边缘故障）就换官网直连，换到没有更多来源为止
    ++m_sourceIndex;
    if (m_sourceIndex < bases.size())
    {
        requestFile(index);
        return;
    }
    finishWithError(tr("第 %1 张封面图下载失败：%2").arg(index).arg(reason));
}

void HomePresetCovers::cancel()
{
    if (!m_downloading)
        return;

    m_downloading = false;
    m_sourceIndex = 0;
    abortReply();
    emit downloadingChanged(false);
}

void HomePresetCovers::finishWithSuccess()
{
    const QStringList files = localFiles();
    if (files.isEmpty())
    {
        finishWithError(tr("封面图下载不完整，请重试"));
        return;
    }

    m_downloading = false;
    m_sourceIndex = 0;
    abortReply();
    emit progressChanged(m_total, m_total);
    emit downloadingChanged(false);
    emit finished(files);
}

void HomePresetCovers::finishWithError(const QString &error)
{
    const bool wasDownloading = m_downloading;
    m_downloading = false;
    m_sourceIndex = 0;
    m_lastError = error;
    abortReply();

    qWarning() << "[HomePresetCovers] 预设封面图下载失败:" << error;
    if (wasDownloading)
        emit downloadingChanged(false);
    emit failed(error);
}

void HomePresetCovers::abortReply()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}
