#include "MasonryContentCard.h"
#include "utils/IconHelper.h"
#include "utils/McimHelper.h"
#include "utils/ThemeManager.h"
#include "utils/mod/ModData.h"

#include <QFont>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QHash>
#include <QImage>
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
constexpr int kCenterLogoSize = 72;// 无封面时徽标在封面区居中放大的边长
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

// 异步加载图片并应用（QPointer 守护卡片生命周期，QPixmapCache 内存缓存）
// onResult：true = 图已应用，false = 加载/解码失败（封面失败时徽标要挪回复中位）
// allowFallback：镜像拿到非图片内容时是否换原始地址重试一次
void loadImageInto(QLabel *label, const QString &url, int w, int h,
                   std::function<QPixmap(const QPixmap &)> transform,
                   std::function<void(bool)> onResult = nullptr,
                   bool allowFallback = true)
{
    if (url.isEmpty() || !label) {
        if (onResult)
            onResult(false);
        return;
    }
    const QString cacheKey = QStringLiteral("card_img:%1:%2x%3").arg(url).arg(w).arg(h);
    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        label->setPixmap(transform(cached));
        if (onResult)
            onResult(true);
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
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, guard, cacheKey, transform, onResult, label, url, w, h, allowFallback]() {
        reply->deleteLater();
        if (!guard)
            return;
        QPixmap pm;
        if (reply->error() == QNetworkReply::NoError)
            pm.loadFromData(reply->readAll());
        if (pm.isNull()) {
            // 镜像图片域名会返回网页（HTTP 200 + text/html）：记住并改用原始地址再试一次
            if (allowFallback && McimHelper::rewriteImageUrl(url) != url) {
                McimHelper::reportImageHostBroken();
                loadImageInto(label, url, w, h, transform, onResult, false);
                return;
            }
            if (onResult)
                onResult(false);
            return;
        }
        QPixmapCache::insert(cacheKey, pm);
        guard->setPixmap(transform(pm));
        if (onResult)
            onResult(true);
    });
}

} // namespace

// 封面图：KeepAspectRatioByExpanding 居中裁剪 + 圆角 16
QPixmap MasonryContentCard::makeCoverPixmap(const QPixmap &src, int w, int h)
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
// logo 图标：居中裁剪 + 圆角 + 白色描边
QPixmap MasonryContentCard::makeRoundIcon(const QPixmap &src, int size)
{
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    const int stroke = 3;
    const int inner = size - stroke;
    const qreal radius = size * 0.23;
    QPixmap scaled = src.scaled(inner, inner, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int sx = (scaled.width() - inner) / 2;
    const int sy = (scaled.height() - inner) / 2;
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    const qreal inset = stroke / 2.0;
    clip.addRoundedRect(QRectF(inset, inset, inner, inner), radius, radius);
    p.setClipPath(clip);
    p.drawPixmap(inset - sx, inset - sy, scaled);
    p.setClipping(false);
    p.setPen(QPen(QColor(255, 255, 255, 245), stroke));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(inset, inset, inner, inner), radius, radius);
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

QPixmap MasonryContentCard::makeLogoPixmap(const QColor &c1, const QColor &c2, const QString &letter,
                                            int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal radius = size * 0.23;
    p.setPen(QPen(QColor(255, 255, 255, 245), 3));
    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0.0, c1);
    grad.setColorAt(1.0, c2);
    p.setBrush(grad);
    p.drawRoundedRect(QRectF(1.5, 1.5, size - 3, size - 3), radius, radius);

    p.setPen(Qt::white);
    QFont font;
    font.setBold(true);
    font.setPixelSize(qRound(size * 0.42));   // 52 → 22px，与原字号一致
    p.setFont(font);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, letter);

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
        { QColor(239, 68, 68),   QColor(185, 28, 28)   },  // red
        { QColor(20, 184, 166),  QColor(15, 118, 110)  },  // teal
        { QColor(14, 165, 233),  QColor(3, 105, 161)   },  // sky
        { QColor(249, 115, 22),  QColor(194, 65, 12)   },  // orange
        { QColor(244, 63, 94),   QColor(159, 18, 57)   },  // rose
        { QColor(217, 70, 239),  QColor(162, 28, 175)  },  // fuchsia
        { QColor(168, 85, 247),  QColor(126, 34, 206)  },  // purple
        { QColor(101, 163, 13),  QColor(66, 111, 8)    },  // olive
        { QColor(59, 130, 246),  QColor(29, 78, 216)   }   // blue
    };
    const QString seed = info.source + ":" + info.id;
    const uint h = qHash(seed);
    const auto &p = kPalette.at(h % kPalette.size());
    c1 = p.first;
    c2 = p.second;
    shapePairForTheme(c1, c2);
}

void MasonryContentCard::shapePairForTheme(QColor &c1, QColor &c2)
{
    const bool dark = ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    if (dark) {
        // 深色模式：整对压暗，避免亮色块在暗底上刺眼
        c1 = c1.darker(130);
        c2 = c2.darker(130);
    } else {
        // 浅色模式：整对提亮
        c1 = c1.lighter(118);
        c2 = c2.lighter(118);
    }
}

bool MasonryContentCard::paletteForIcon(const QPixmap &icon, QColor &c1, QColor &c2)
{
    if (icon.isNull())
        return false;
    const QImage img = icon.toImage().convertToFormat(QImage::Format_ARGB32);
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
        return false;

    // 16 级量化直方图：统计各色桶像素数，之后在"够大"的色桶里挑最抢眼的主色
    struct Bucket { int n = 0; int r = 0; int g = 0; int b = 0; };
    QHash<int, Bucket> hist;
    int total = 0;
    const int stepX = qMax(1, img.width() / 96);
    const int stepY = qMax(1, img.height() / 96);
    for (int y = 0; y < img.height(); y += stepY) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += stepX) {
            const QRgb px = line[x];
            if (qAlpha(px) < 160)
                continue;
            const int key = ((qRed(px) >> 4) << 8) | ((qGreen(px) >> 4) << 4) | (qBlue(px) >> 4);
            Bucket &bk = hist[key];
            ++bk.n;
            bk.r += qRed(px);
            bk.g += qGreen(px);
            bk.b += qBlue(px);
            ++total;
        }
    }
    if (total <= 0)
        return false;
    // 过小的色块（抗锯齿碎屑、噪点）没资格当选主色
    const int minN = qMax(4, total / 200);

    double bestScore = 0.0;
    QColor best;
    for (auto it = hist.constBegin(); it != hist.constEnd(); ++it) {
        const Bucket &bk = it.value();
        if (bk.n < minN)
            continue;
        const QColor avg(bk.r / bk.n, bk.g / bk.n, bk.b / bk.n);
        // 灰/白/黑撑不起色系（白描边、透明底、阴影都会落进这些桶）
        if (avg.hsvSaturationF() < 0.22 || avg.valueF() < 0.18 || avg.valueF() > 0.96)
            continue;
        // 抢眼度 = 绝对色彩度（max-min）× 亮度加权。
        // 不用 HSV 饱和度：它偏爱深棕这类"暗而饱和"的颜色（草方块土色 sat 0.52 会压过草绿 0.43），
        // 绝对色彩度 + 亮度更贴近肉眼的"鲜艳"
        const int r = bk.r / bk.n, g = bk.g / bk.n, b = bk.b / bk.n;
        const double chroma = qMax(r, qMax(g, b)) - qMin(r, qMin(g, b));
        const double score = chroma * (0.7 + 0.3 * avg.valueF());
        if (score > bestScore) {
            bestScore = score;
            best = avg;
        }
    }
    if (!best.isValid())
        return false;

    // 主色 → 明暗展开成渐变对 → 按主题整形（浅亮 / 深暗）
    c1 = best.lighter(115);
    c2 = best.darker(115);
    shapePairForTheme(c1, c2);
    return true;
}

int MasonryContentCard::bannerHeightFor(const ModInfo &info)
{
    Q_UNUSED(info);
    // 封面高度固定，卡片高度差异由内容（描述文字行数）自然决定
    return kBannerH;
}

int MasonryContentCard::logoSizeFor(bool centered, int bannerH)
{
    // 居中款四周留空：上下至少各 12px，矮 banner（96）下自动收到 72
    return centered ? qMin(kCenterLogoSize, bannerH - 24) : kLogoSize;
}

void MasonryContentCard::placeLogo(QLabel *logo, bool centered, int bannerW, int bannerH)
{
    if (!logo)
        return;
    // 徽标尺寸会切换（居中 ↔ 右下），让 QLabel 按标签框缩放已设的 pixmap，避免裁剪
    logo->setScaledContents(true);
    logo->setFixedSize(logoSizeFor(centered, bannerH), logoSizeFor(centered, bannerH));
    if (centered)
        logo->move((bannerW - logo->width()) / 2, (bannerH - logo->height()) / 2);
    else
        logo->move(bannerW - kLogoSize - 12, bannerH - (kLogoSize - kLogoOverlap));
}

void MasonryContentCard::loadCoverInto(QLabel *banner, const QString &url, int width, int height)
{
    loadImageInto(banner, url, width, height,
                  [width, height](const QPixmap &pm) { return makeCoverPixmap(pm, width, height); },
                  // 封面到位 → 徽标从封面区正中收回右下角半压；失败则维持居中
                  [banner, width, height](bool ok) {
                      if (!ok)
                          return;
                      banner->setProperty("gradientBanner", false);
                      if (auto *logo = banner->findChild<QLabel *>(QStringLiteral("contentCardLogo")))
                          placeLogo(logo, false, width, height);
                  });
}

QWidget *MasonryContentCard::build(const ModInfo &info, QWidget *parent,
                                   const QList<ActionSpec> &actions)
{
    QColor c1, c2;
    paletteFor(info, c1, c2);
    const int bannerH = bannerHeightFor(info);

    // 居中徽标下方的 banner 底色优先匹配图标色系；拿不到图标才回退散列色板
    QColor banner1 = c1, banner2 = c2;
    QPixmap localIcon;
    if (!info.iconUrl.isEmpty() && info.iconUrl.startsWith(QLatin1String(":/")))
        localIcon = QPixmap(info.iconUrl);
    paletteForIcon(localIcon, banner1, banner2);

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

    // 封面 banner：先用渐变占位（图标同色系 / 散列色板），coverUrl 返回后异步替换为真实封面
    QLabel *banner = new QLabel(card);
    banner->setObjectName("contentCardBanner");
    banner->setFixedHeight(bannerH);
    banner->setPixmap(makeBannerPixmap(banner1, banner2, bannerW, bannerH));
    banner->setProperty("gradientBanner", true);   // 封面真正盖上后置 false
    cardLay->addWidget(banner);

    // 徽标：拿不到封面时在封面区正中放大（四周留空，不放右下角）；
    // 封面真正加载成功后收回右下角半压。先用字母渐变占位。
    // CurseForge 图标风格不统一，不加载真实图标（保持渐变字母）；其余来源加载真实图标
    QLabel *logo = new QLabel(banner);
    logo->setObjectName("contentCardLogo");
    logo->setAlignment(Qt::AlignCenter);
    const int badgeSize = logoSizeFor(true, bannerH);
    logo->setPixmap(makeLogoPixmap(c1, c2, letter, badgeSize));
    if (!info.iconUrl.isEmpty() && info.source != QStringLiteral("curseforge")) {
        if (!localIcon.isNull()) {
            // 本地图标（透明底方块图）：直接铺到 logo 区，不画渐变底板
            QPixmap badge(badgeSize, badgeSize);
            badge.fill(Qt::transparent);
            const QPixmap glyph = localIcon.scaled(qRound(badgeSize * 0.85), qRound(badgeSize * 0.85),
                                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPainter painter(&badge);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.drawPixmap((badgeSize - glyph.width()) / 2,
                               (badgeSize - glyph.height()) / 2, glyph);
            painter.end();
            logo->setPixmap(badge);
        } else if (!info.iconUrl.startsWith(QLatin1String(":/"))) {
            loadImageInto(logo, info.iconUrl, badgeSize, badgeSize,
                          [badgeSize](const QPixmap &pm) { return makeRoundIcon(pm, badgeSize); },
                          // 网络图标到位且 banner 仍是渐变占位时，把底色换成图标同色系
                          [logoW = QPointer<QLabel>(logo), banner, bannerW, bannerH](bool ok) {
                              if (!ok || !logoW)
                                  return;
                              if (!banner->property("gradientBanner").toBool())
                                  return;
                              QColor i1, i2;
                              if (paletteForIcon(logoW->pixmap(), i1, i2))
                                  banner->setPixmap(makeBannerPixmap(i1, i2, bannerW, bannerH));
                          });
        }
    }
    placeLogo(logo, true, bannerW, bannerH);
    // 给 logo 加深色投影，确保图标/字母在封面/渐变底上清晰可见
    auto *logoShadow = new QGraphicsDropShadowEffect(logo);
    logoShadow->setBlurRadius(4);
    logoShadow->setOffset(0, 1);
    logoShadow->setColor(QColor(0, 0, 0, 180));
    logo->setGraphicsEffect(logoShadow);
    logo->raise();

    // 封面加载成功 → 徽标收回右下角半压；失败/无封面 → 维持居中放大
    if (!info.coverUrl.isEmpty()) {
        loadImageInto(banner, info.coverUrl, bannerW, bannerH,
                      [bannerW, bannerH](const QPixmap &pm) { return makeCoverPixmap(pm, bannerW, bannerH); },
                      [logoW = QPointer<QLabel>(logo), banner, bannerW, bannerH](bool ok) {
                          if (ok)
                              banner->setProperty("gradientBanner", false);
                          if (logoW)
                              placeLogo(logoW, !ok, bannerW, bannerH);
                      });
    }

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
