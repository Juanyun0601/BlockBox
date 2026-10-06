#include "MasonryContentCard.h"
#include "utils/IconHelper.h"
#include "utils/McimHelper.h"
#include "utils/ThemeManager.h"
#include "utils/mod/ModData.h"

#include <QFont>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDir>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPixmapCache>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

constexpr int kCardWidth = 260;
constexpr int kCardBorder = 1;     // 与 QSS #contentCard border 宽度一致（banner 内缩，避免盖住边框）
constexpr int kLogoSize = 52;
constexpr int kLogoOverlap = 10;   // logo 压出 banner 下缘的像素数（减小压出，避免过低）
constexpr int kBannerH = 120;      // 封面固定高度（瀑布流高度由描述文字决定）

// ── 卡片图片网络加载（QNetworkAccessManager + 磁盘缓存 + QPixmapCache） ──
QNetworkAccessManager *s_cardImageNAM = nullptr;
QNetworkDiskCache *s_cardImageCache = nullptr;

QNetworkAccessManager *cardImageNAM()
{
    if (!s_cardImageNAM) {
        s_cardImageNAM = new QNetworkAccessManager();
        s_cardImageCache = new QNetworkDiskCache();
        const QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                  + QStringLiteral("/card_images");
        QDir().mkpath(cachePath);
        s_cardImageCache->setCacheDirectory(cachePath);
        s_cardImageCache->setMaximumCacheSize(100 * 1024 * 1024);
        s_cardImageNAM->setCache(s_cardImageCache);
    }
    return s_cardImageNAM;
}

// 封面图：KeepAspectRatioByExpanding 居中裁剪 + 圆角 16
QPixmap makeCoverPixmap(const QPixmap &src, int w, int h)
{
    QPixmap out(w, h);
    out.fill(Qt::transparent);
    QPixmap scaled = src.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int sx = (scaled.width() - w) / 2;
    const int sy = (scaled.height() - h) / 2;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, w, h), 16, 16);
    p.setClipPath(clip);
    p.drawPixmap(-sx, -sy, scaled);
    return out;
}

// 异步加载图片并应用（QPointer 守护卡片生命周期，QPixmapCache 内存缓存）
void loadImageInto(QLabel *label, const QString &url, int w, int h,
                   std::function<QPixmap(const QPixmap &)> transform)
{
    if (url.isEmpty() || !label)
        return;
    const QString cacheKey = QStringLiteral("card_img:%1:%2x%3").arg(url).arg(w).arg(h);
    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        label->setPixmap(transform(cached));
        return;
    }

    QPointer<QLabel> guard(label);
    QString acceleratedUrl = McimHelper::rewriteImageUrl(url);
    QNetworkRequest request{QUrl(acceleratedUrl)};
    // wiki 等图源走 HTTP/2 时并发流会被流控卡死（实测 40 并发 0 完成），
    // 强制 HTTP/1.1 走 Qt 每主机 6 连接，封面才能陆续回来
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    // wiki 图床对无 User-Agent / 伪装浏览器 UA 返回 403，需带明确客户端标识
    request.setRawHeader("User-Agent", "BlockBox/1.0 (Minecraft version covers)");
    QNetworkReply *reply = cardImageNAM()->get(request);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, guard, cacheKey, transform]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || !guard)
            return;
        QPixmap pm;
        pm.loadFromData(reply->readAll());
        if (pm.isNull())
            return;
        QPixmapCache::insert(cacheKey, pm);
        guard->setPixmap(transform(pm));
    });
}

} // namespace
// logo 图标：居中裁剪 + 圆角 12 + 白色描边
QPixmap MasonryContentCard::makeRoundIcon(const QPixmap &src, int size)
{
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    const int inner = size - 3;
    QPixmap scaled = src.scaled(inner, inner, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int sx = (scaled.width() - inner) / 2;
    const int sy = (scaled.height() - inner) / 2;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(1.5, 1.5, inner, inner), 12, 12);
    p.setClipPath(clip);
    p.drawPixmap(1.5 - sx, 1.5 - sy, scaled);
    p.setClipping(false);
    p.setPen(QPen(QColor(255, 255, 255, 245), 3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(1.5, 1.5, inner, inner), 12, 12);
    return out;
}


QPixmap MasonryContentCard::makeBannerPixmap(const QColor &c1, const QColor &c2, int width, int height)
{
    const int w = width;
    QPixmap pix(w, height);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, w, height);
    grad.setColorAt(0.0, c1);
    grad.setColorAt(1.0, c2);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRoundedRect(QRectF(0, 0, w, height), 16, 16);

    // 光影装饰（对齐原型 .content-card-banner::after）
    QRadialGradient glow(w * 0.2, height * 0.3, w * 0.7);
    glow.setColorAt(0.0, QColor(255, 255, 255, 60));
    glow.setColorAt(0.55, Qt::transparent);
    p.setBrush(glow);
    p.drawRoundedRect(QRectF(0, 0, w, height), 16, 16);

    p.end();
    return pix;
}

QPixmap MasonryContentCard::makeLogoPixmap(const QColor &c1, const QColor &c2, const QString &letter)
{
    QPixmap pix(kLogoSize, kLogoSize);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    p.setPen(QPen(QColor(255, 255, 255, 245), 3));
    QLinearGradient grad(0, 0, kLogoSize, kLogoSize);
    grad.setColorAt(0.0, c1);
    grad.setColorAt(1.0, c2);
    p.setBrush(grad);
    p.drawRoundedRect(QRectF(1.5, 1.5, kLogoSize - 3, kLogoSize - 3), 12, 12);

    p.setPen(Qt::white);
    QFont font;
    font.setBold(true);
    font.setPixelSize(22);
    p.setFont(font);
    p.drawText(QRectF(0, 0, kLogoSize, kLogoSize), Qt::AlignCenter, letter);

    p.end();
    return pix;
}

void MasonryContentCard::paletteFor(const ModInfo &info, QColor &c1, QColor &c2)
{
    static const QList<QPair<QColor, QColor>> kPalette = {
        { QColor(16, 185, 129),  QColor(4, 120, 87)   },   // emerald
        { QColor(99, 102, 241),  QColor(67, 56, 202)   },  // indigo
        { QColor(245, 158, 11),  QColor(217, 119, 6)   },  // amber
        { QColor(236, 72, 153),  QColor(190, 24, 93)   },  // pink
        { QColor(139, 92, 246),  QColor(109, 40, 217)  },  // violet
        { QColor(6, 182, 212),   QColor(14, 116, 144)  },  // cyan
        { QColor(132, 204, 22),  QColor(77, 124, 15)   },  // lime
        { QColor(239, 68, 68),   QColor(185, 28, 28)   }   // red
    };
    const QString seed = info.source + ":" + info.id;
    const uint h = qHash(seed);
    const auto &p = kPalette.at(h % kPalette.size());
    c1 = p.first;
    c2 = p.second;
}

int MasonryContentCard::bannerHeightFor(const ModInfo &info)
{
    Q_UNUSED(info);
    // 封面高度固定，卡片高度差异由内容（描述文字行数）自然决定
    return kBannerH;
}

void MasonryContentCard::loadCoverInto(QLabel *banner, const QString &url, int width, int height)
{
    loadImageInto(banner, url, width, height,
                  [width, height](const QPixmap &pm) { return makeCoverPixmap(pm, width, height); });
}

QWidget *MasonryContentCard::build(const ModInfo &info, QWidget *parent,
                                   const QList<ActionSpec> &actions)
{
    QColor c1, c2;
    paletteFor(info, c1, c2);
    const int bannerH = bannerHeightFor(info);

    QString displayName = info.chineseName.isEmpty() ? info.name : info.chineseName;
    QString letter = displayName.trimmed().isEmpty() ? QStringLiteral("?")
                     : displayName.trimmed().left(1).toUpper();

    QWidget *card = new QWidget(parent);
    card->setObjectName("contentCard");
    card->setFixedWidth(kCardWidth);
    card->setCursor(Qt::PointingHandCursor);
    applyShadow(card);

    QVBoxLayout *cardLay = new QVBoxLayout(card);
    // banner 内缩 kCardBorder，避免占满顶部盖住卡片边框（QSS #contentCard border）
    cardLay->setContentsMargins(kCardBorder, kCardBorder, kCardBorder, kCardBorder);
    cardLay->setSpacing(0);
    const int bannerW = kCardWidth - 2 * kCardBorder;

    // 封面 banner：先用渐变占位，coverUrl 返回后异步替换为真实封面
    QLabel *banner = new QLabel(card);
    banner->setObjectName("contentCardBanner");
    banner->setFixedHeight(bannerH);
    banner->setPixmap(makeBannerPixmap(c1, c2, bannerW, bannerH));
    cardLay->addWidget(banner);
    if (!info.coverUrl.isEmpty()) {
        loadImageInto(banner, info.coverUrl, bannerW, bannerH,
                      [bannerW, bannerH](const QPixmap &pm) { return makeCoverPixmap(pm, bannerW, bannerH); });
    }

    // 半压封面右下 logo：先用字母渐变占位。
    // CurseForge 图标风格不统一，不加载真实图标（保持渐变字母）；其余来源加载真实图标
    QLabel *logo = new QLabel(banner);
    logo->setObjectName("contentCardLogo");
    logo->setFixedSize(kLogoSize, kLogoSize);
    logo->setAlignment(Qt::AlignCenter);
    logo->setPixmap(makeLogoPixmap(c1, c2, letter));
    logo->move(bannerW - kLogoSize - 12, bannerH - (kLogoSize - kLogoOverlap));
    if (!info.iconUrl.isEmpty() && info.source != QStringLiteral("curseforge")) {
        if (info.iconUrl.startsWith(QLatin1String(":/"))) {
            // 本地图标（透明底方块图）：直接铺到 logo 区，不画渐变底板
            const QPixmap block(info.iconUrl);
            if (!block.isNull()) {
                QPixmap badge(kLogoSize, kLogoSize);
                badge.fill(Qt::transparent);
                const QPixmap glyph = block.scaled(44, 44, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
                QPainter painter(&badge);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.drawPixmap((kLogoSize - glyph.width()) / 2,
                                   (kLogoSize - glyph.height()) / 2, glyph);
                painter.end();
                logo->setPixmap(badge);
            }
        } else {
            loadImageInto(logo, info.iconUrl, kLogoSize, kLogoSize,
                          [](const QPixmap &pm) { return MasonryContentCard::makeRoundIcon(pm, kLogoSize); });
        }
    }
    // 给 logo 加深色投影，确保图标/字母在半压区域上清晰可见
    auto *logoShadow = new QGraphicsDropShadowEffect(logo);
    logoShadow->setBlurRadius(4);
    logoShadow->setOffset(0, 1);
    logoShadow->setColor(QColor(0, 0, 0, 180));
    logo->setGraphicsEffect(logoShadow);
    logo->raise();

    // 内容区
    QWidget *body = new QWidget(card);
    QVBoxLayout *bv = new QVBoxLayout(body);
    bv->setContentsMargins(12, 14, 12, 12);
    bv->setSpacing(6);

    QLabel *nameLabel = new QLabel(displayName, body);
    nameLabel->setObjectName("modCardName");   // 复用 findChild 兼容逻辑
    bv->addWidget(nameLabel);

    QLabel *descLabel = new QLabel(info.description, body);
    descLabel->setObjectName("contentCardDesc");
    descLabel->setWordWrap(true);
    // 不固定高度、不截断：文字多则卡片自然更高（瀑布流高度随内容）
    bv->addWidget(descLabel);

    // chips：版本 / 加载器 / 分类 / 更新时间 + 下载量
    QWidget *chipsWidget = new QWidget(body);
    chipsWidget->setObjectName("contentCardChips");
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);
    auto addChip = [&](const QString &text) {
        QLabel *chip = new QLabel(text);
        chip->setObjectName("contentChip");
        chipsLayout->addWidget(chip);
    };
    if (!info.gameVersions.isEmpty()) addChip(info.gameVersions.first());
    if (!info.loaders.isEmpty()) addChip(info.loaders.first());
    if (!info.categories.isEmpty()) addChip(info.categories.first());
    if (info.downloadCount > 0)
        addChip(QWidget::tr("%1 下载").arg(info.downloadCount));
    chipsLayout->addStretch();
    bv->addWidget(chipsWidget);

    bv->addStretch();

    // 操作按钮
    if (!actions.isEmpty()) {
        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(4);
        btnLayout->setContentsMargins(0, 4, 0, 0);
        QColor themeColor(ThemeManager::instance()->currentThemeColor());
        for (const ActionSpec &spec : actions) {
            QPushButton *btn = new QPushButton();
            btn->setObjectName("contentCardActionBtn");
            btn->setFixedSize(32, 32);
            btn->setToolTip(spec.tooltip);
            btn->setCursor(Qt::PointingHandCursor);
            const QColor iconColor = spec.iconColor.isValid() ? spec.iconColor : themeColor;
            btn->setIcon(IconHelper::loadColoredIcon(spec.iconPath, iconColor, 18));
            btn->setIconSize(QSize(18, 18));
            if (spec.onClick)
                QObject::connect(btn, &QPushButton::clicked, btn, [cb = spec.onClick]() { cb(); });
            btnLayout->addWidget(btn);
        }
        btnLayout->addStretch();
        bv->addLayout(btnLayout);
    }

    cardLay->addWidget(body);
    return card;
}

void MasonryContentCard::applyShadow(QWidget *w, int blurRadius, int offsetY, int alpha)
{
    if (!w)
        return;
    auto *shadow = new QGraphicsDropShadowEffect(w);
    shadow->setBlurRadius(blurRadius);
    shadow->setOffset(0, offsetY);
    shadow->setColor(QColor(0, 0, 0, alpha));
    w->setGraphicsEffect(shadow);
}
