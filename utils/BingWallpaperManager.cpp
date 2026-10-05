/**
 * @file   BingWallpaperManager.cpp
 * @brief  必应每日壁纸管理器实现
 * @author BlockBox Team
 * @date   2026-10-05
 */

#include "BingWallpaperManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include "BackgroundManager.h"
#include "../platform.h"

namespace {

constexpr int TIMEOUT_MS = 15000;
// 必应官方壁纸归档接口：返回最近 8 天的每日壁纸元数据
const char kArchiveUrl[] =
    "https://cn.bing.com/HPImageArchive.aspx?format=js&idx=0&n=8&mkt=zh-CN";
const char kBaseUrl[] = "https://cn.bing.com";

QNetworkReply* getWithTimeout(QNetworkAccessManager *nam, const QUrl &url, QTimer *&timerOut)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam->get(request);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(TIMEOUT_MS);
    timerOut = timer;
    return reply;
}

} // namespace

BingWallpaperManager* BingWallpaperManager::m_instance = nullptr;
QMutex BingWallpaperManager::m_instanceMutex;

BingWallpaperManager::BingWallpaperManager(QObject *parent)
    : QObject(parent)
{
    m_networkManager = new QNetworkAccessManager(this);
    m_currentIndex = QSettings("BlockBox", "Settings")
                         .value("background/bingIndex", 0).toInt();
}

BingWallpaperManager* BingWallpaperManager::instance()
{
    if (!m_instance) {
        QMutexLocker locker(&m_instanceMutex);
        if (!m_instance) {
            m_instance = new BingWallpaperManager();
        }
    }
    return m_instance;
}

QString BingWallpaperManager::cacheDir()
{
    const QString dir = Platform::getDataDirectory() + "/cache/bing";
    QDir().mkpath(dir);
    return dir;
}

QString BingWallpaperManager::normalizeUrlbase(const QString &urlbase)
{
    // "/th?id=OHR.Xxx_EN-US123" -> "Xxx_EN-US123"，作为稳定的缓存文件名
    QString id = urlbase;
    int pos = id.indexOf("id=OHR.");
    if (pos >= 0) {
        id = id.mid(pos + QString("id=OHR.").size());
    } else {
        id = id.section('/', -1);
    }
    QString out;
    for (const QChar &ch : id) {
        if (ch.isLetterOrNumber() || ch == '_' || ch == '-') {
            out.append(ch);
        }
    }
    return out;
}

QString BingWallpaperManager::buildImageUrl(const QString &urlbase, int width, int height)
{
    return QString("%1%2_%3x%4.jpg").arg(kBaseUrl, urlbase).arg(width).arg(height);
}

QString BingWallpaperManager::cacheFileName(const WallpaperInfo &info, const QString &suffix) const
{
    QString id = normalizeUrlbase(info.urlbase);
    if (id.isEmpty()) {
        id = info.hsh.isEmpty() ? info.date : info.hsh;
    }
    return QString("%1/%2%3.jpg").arg(cacheDir(), id, suffix);
}

QString BingWallpaperManager::currentImagePath() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_wallpapers.size()) {
        return QString();
    }
    WallpaperInfo info = m_wallpapers.at(m_currentIndex);
    const QString path = cacheFileName(info, QString());
    return QFile::exists(path) ? path : QString();
}

void BingWallpaperManager::ensureLoaded()
{
    if (m_wallpapers.isEmpty() && !m_listLoading) {
        fetchList();
        return;
    }
    ensureCurrentImage();
}

void BingWallpaperManager::refresh()
{
    fetchList();
}

void BingWallpaperManager::fetchList()
{
    if (m_listLoading) {
        return;
    }
    m_listLoading = true;

    QTimer *timer = nullptr;
    QNetworkReply *reply = getWithTimeout(m_networkManager, QUrl(kArchiveUrl), timer);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer]() {
        m_listLoading = false;
        if (timer) timer->stop();

        if (reply->error() != QNetworkReply::NoError) {
            emit fetchFailed(tr("网络请求失败：%1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();

        const QJsonArray images = doc.object().value("images").toArray();
        if (images.isEmpty()) {
            emit fetchFailed(tr("必应接口返回数据为空"));
            return;
        }

        QList<WallpaperInfo> newList;
        for (const QJsonValue &v : images) {
            const QJsonObject obj = v.toObject();
            WallpaperInfo info;
            info.date = obj.value("enddate").toString();
            info.urlbase = obj.value("urlbase").toString();
            info.title = obj.value("title").toString();
            info.copyright = obj.value("copyright").toString();
            info.hsh = obj.value("hsh").toString();
            // 接口返回的 url 自带尺寸后缀与参数，直接拼接域名即为原图地址
            info.fullUrl = kBaseUrl + obj.value("url").toString();
            newList.append(info);
        }

        const bool wasEmpty = m_wallpapers.isEmpty();
        m_wallpapers = newList;

        // 手动刷新后定位到最新一天；首次加载沿用上次的选择
        if (!wasEmpty) {
            m_currentIndex = 0;
        }
        m_currentIndex = qBound(0, m_currentIndex, m_wallpapers.size() - 1);
        QSettings("BlockBox", "Settings").setValue("background/bingIndex", m_currentIndex);

        emit wallpaperListUpdated();
        downloadMissingThumbs();
        ensureCurrentImage();
    });
}

QString BingWallpaperManager::thumbPath(int index) const
{
    if (index < 0 || index >= m_wallpapers.size()) {
        return QString();
    }
    return cacheFileName(m_wallpapers.at(index), "_thumb");
}

void BingWallpaperManager::downloadMissingThumbs()
{
    for (int i = 0; i < m_wallpapers.size(); ++i) {
        const WallpaperInfo info = m_wallpapers.at(i);
        const QString path = cacheFileName(info, "_thumb");
        if (QFile::exists(path)) {
            emit wallpaperThumbReady(i, path);
            continue;
        }
        // 缩略图直接复用原图地址并附加缩放参数；参数被忽略时顶多拿到原图，UI 会自行缩小
        QString thumbUrl = info.fullUrl.isEmpty()
                               ? buildImageUrl(info.urlbase, 1920, 1080)
                               : info.fullUrl;
        if (!thumbUrl.contains('?')) {
            thumbUrl.append('?');
        }
        thumbUrl.append("&w=272&h=153&c=7");

        // 缩略图属非关键资源，失败时静默忽略，UI 端显示占位样式即可
        downloadImage(thumbUrl, path,
            [this, i, path](const QString &) {
                emit wallpaperThumbReady(i, path);
            },
            [](const QString &reason) {
                Q_UNUSED(reason);
            });
    }
}

void BingWallpaperManager::selectWallpaper(int index)
{
    if (index < 0 || index >= m_wallpapers.size() || index == m_currentIndex) {
        return;
    }
    m_currentIndex = index;
    QSettings("BlockBox", "Settings").setValue("background/bingIndex", index);
    ensureCurrentImage();
}

void BingWallpaperManager::ensureCurrentImage()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_wallpapers.size() || m_imageDownloading) {
        return;
    }

    const WallpaperInfo info = m_wallpapers.at(m_currentIndex);
    const QString path = cacheFileName(info, QString());
    if (QFile::exists(path)) {
        emit wallpaperImageReady(path);
        BackgroundManager::instance()->setBingImagePath(path);
        return;
    }

    m_imageDownloading = true;
    downloadImage(info.fullUrl, path,
        [this, path](const QString &) {
            m_imageDownloading = false;
            emit wallpaperImageReady(path);
            BackgroundManager::instance()->setBingImagePath(path);
        },
        [this](const QString &reason) {
            m_imageDownloading = false;
            emit fetchFailed(tr("壁纸下载失败：%1").arg(reason));
        });
}

void BingWallpaperManager::downloadImage(const QString &url, const QString &savePath,
                                         const std::function<void(const QString &)> &onSuccess,
                                         const std::function<void(const QString &)> &onFailure)
{
    QTimer *timer = nullptr;
    QNetworkReply *reply = getWithTimeout(m_networkManager, QUrl(url), timer);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, timer, savePath, onSuccess, onFailure]() {
        if (timer) timer->stop();

        if (reply->error() != QNetworkReply::NoError) {
            onFailure(reply->errorString());
            reply->deleteLater();
            return;
        }

        const QByteArray data = reply->readAll();
        reply->deleteLater();

        if (data.isEmpty()) {
            onFailure(tr("下载数据为空"));
            return;
        }

        QFile file(savePath);
        const bool saved = file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        file.close();
        if (!saved) {
            // 清掉半截文件，避免下次被当作有效缓存
            QFile::remove(savePath);
            onFailure(tr("写入缓存失败"));
            return;
        }

        onSuccess(savePath);
    });
}
