/**
 * @file   StructureIcons.cpp
 * @brief  结构图标提供器：内存 → 磁盘 → 远程 API（Wiki）→ 内置兜底
 * @author BlockBox Team
 * @date   2026-10-11
 *
 * Icon 风格直下 `zh.minecraft.wiki/images/EnvSprite_*.png`（生成结构表
 * 文字旁的 16px 小图标）；Overview 风格走 MediaWiki pageimages 查条目主图。
 * 两风格磁盘缓存文件名分开，切换不互相覆盖。
 */

#include "StructureIcons.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include "platform.h"

namespace StructureIcons {

namespace {

/** 内存缓存：key+尺寸+风格 → 图标 */
QHash<QString, QPixmap> g_mem;
/** 正在发起（含查询 URL 与下载图片两段）的 key，防重复请求 */
QSet<QString> g_inflight;
/** 远程图标全部就位 / 失败后的重绘通知 */
std::function<void()> g_onUpdated;
/** 当前图标风格（默认文字旁小图标） */
Style g_style = Style::Icon;

QNetworkAccessManager *nam()
{
    static QNetworkAccessManager m;
    return &m;
}

QString cacheDir()
{
    const QString dir = Platform::getDataDirectory()
                      + QStringLiteral("/cache/structure_icons");
    QDir().mkpath(dir);
    return dir;
}

QString styleSuffix(Style s)
{
    return s == Style::Icon ? QStringLiteral(".icon") : QString();
}

/** 远程图标（真图片）缓存路径：按结构 key + 风格存 PNG */
QString imagePath(const QString &key, Style s)
{
    return cacheDir() + QLatin1Char('/') + key + styleSuffix(s)
         + QStringLiteral(".png");
}

/** 内置兜底图：圆形徽章 + 结构首字 + 按下标分散的配色（离线也好看） */
QPixmap builtinPixmap(const QString &firstChar, int structIdx, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int hue = (structIdx * 137) % 360;
    const QColor bg = QColor::fromHsv(hue, 132, 198);
    p.setPen(QPen(QColor(255, 255, 255, 235), qMax(1, size / 14)));
    p.setBrush(bg);
    p.drawEllipse(QRectF(1, 1, size - 2, size - 2));

    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setFamily(QStringLiteral("Microsoft YaHei"));
    f.setPixelSize(qMax(8, int(size * 0.52)));
    p.setFont(f);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, firstChar);
    p.end();
    return pm;
}

void notifyUpdated()
{
    if (g_onUpdated)
        g_onUpdated();
}

/** 下载远程图标 → 落盘 → 进内存 → 通知重绘 */
void downloadImage(const QString &imageUrl, const QString &memKey,
                   const QString &diskPath)
{
    QNetworkRequest req{QUrl(imageUrl)};
    req.setTransferTimeout(15000);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("BlockBox/1.0 (structure icons)"));
    QNetworkReply *reply = nam()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, memKey, diskPath]() {
        reply->deleteLater();
        const QString key = memKey.section(QLatin1Char('#'), 0, 0);
        g_inflight.remove(key);
        if (reply->error() != QNetworkReply::NoError)
            return; // 失败就保持内置图标，本次不再重试
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data))
            return;
        QFile f(diskPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            f.write(data);
            f.close();
        }
        g_mem.insert(memKey, pm);
        notifyUpdated();
    });
}

/** Overview 风格第一步：查 Wiki 条目的缩略图地址，再进入下载 */
void fetchThumbUrl(const QString &key, const QString &title,
                   const QString &memKey)
{
    QUrl url(QStringLiteral("https://minecraft.wiki/api.php"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    q.addQueryItem(QStringLiteral("prop"), QStringLiteral("pageimages"));
    q.addQueryItem(QStringLiteral("piprop"), QStringLiteral("thumbnail"));
    q.addQueryItem(QStringLiteral("pithumbsize"), QStringLiteral("48"));
    q.addQueryItem(QStringLiteral("titles"), title);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setTransferTimeout(15000);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("BlockBox/1.0 (structure icons)"));
    QNetworkReply *reply = nam()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, key, memKey, title]() {
        reply->deleteLater();
        const QString disk = imagePath(key, Style::Overview);
        if (reply->error() != QNetworkReply::NoError)
        {
            g_inflight.remove(key);
            return;
        }
        const QJsonObject root =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject pages = root.value(QStringLiteral("query"))
                                       .toObject()
                                       .value(QStringLiteral("pages"))
                                       .toObject();
        QString thumb;
        for (auto it = pages.constBegin(); it != pages.constEnd(); ++it)
        {
            const QJsonObject page = it.value().toObject();
            const QString src =
                page.value(QStringLiteral("thumbnail"))
                    .toObject().value(QStringLiteral("source")).toString();
            if (!src.isEmpty())
            {
                thumb = src;
                break;
            }
        }
        if (thumb.isEmpty() && !title.isEmpty())
        {
            g_inflight.remove(key);
            return; // 该条目没有图：保持内置
        }
        downloadImage(thumb, memKey, disk); // 内部负责清 g_inflight
    });
}

} // namespace

Style style()
{
    return g_style;
}

void setStyle(Style s)
{
    if (g_style == s)
        return;
    g_style = s;
    // 两种风格的磁盘缓存相互独立；换风格只需丢内存与在途状态
    g_mem.clear();
    g_inflight.clear();
    notifyUpdated();
}

QPixmap pixmap(int structIdx, const QString &key, const QString &firstChar,
               const QString &wikiTitle, const QString &itemTitle,
               const QString &envSprite, int size,
               const std::function<void()> &onLoaded)
{
    const Style st = g_style;
    const QString memKey = key + QLatin1Char('#') + QString::number(size)
                         + styleSuffix(st);

    const auto cached = g_mem.constFind(memKey);
    if (cached != g_mem.constEnd())
        return *cached;

    // 磁盘缓存
    const QString disk = imagePath(key, st);
    if (QFile::exists(disk))
    {
        QPixmap pm;
        if (pm.load(disk))
        {
            g_mem.insert(memKey, pm);
            return pm;
        }
    }

    // 未联网 / 未命中：先给内置图，并在后台尝试升级为远程图标
    const QPixmap fallback = builtinPixmap(firstChar, structIdx, size);
    if (!g_inflight.contains(key))
    {
        if (st == Style::Icon && !envSprite.isEmpty())
        {
            // Icon：生成结构表文字旁的小图标，直接下载图片文件
            g_inflight.insert(key);
            downloadImage(QStringLiteral("https://zh.minecraft.wiki/images/")
                              + envSprite,
                          memKey, disk);
        }
        else if (!wikiTitle.isEmpty() || !itemTitle.isEmpty())
        {
            // Overview（或 Icon 缺失时回退）：查条目主图
            const QString title = itemTitle.isEmpty() ? wikiTitle : itemTitle;
            g_inflight.insert(key);
            fetchThumbUrl(key, title, memKey);
        }
    }
    if (onLoaded)
        g_onUpdated = onLoaded;
    g_mem.insert(memKey, fallback);
    return fallback;
}

void clearMemoryCache()
{
    g_mem.clear();
    g_inflight.clear();
}

void setUpdatedHandler(std::function<void()> handler)
{
    g_onUpdated = handler;
}

} // namespace StructureIcons
