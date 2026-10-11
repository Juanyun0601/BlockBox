/**
 * @file   masonry_badge_probe.cpp
 * @brief  瀑布流卡片徽标视觉探针：
 *         无封面 → 图标/首字母在封面区正中放大（四周留空、每卡不同色）；
 *         封面异步回填成功 → 徽标收回右下角半压。
 *
 * 输出：
 *   D:/BlockBox/_out/masonry_badge_before.png  （封面回填前：全部居中）
 *   D:/BlockBox/_out/masonry_badge_after.png   （封面回填后：封面卡徽标回右下角）
 */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <cstdio>

#include "components/MasonryContentCard.h"
#include "utils/mod/ModData.h"

namespace {

// 与 ThemeManager::buildTokenTable 同族的浅色 token 表（探针用精简版）
QString resolveTokens(QString style)
{
    struct KV { const char *k; const char *v; };
    const QVector<KV> t = {
        {"@BG_BASE@", "#f5f5f5"}, {"@BG_CARD@", "#ffffff"},
        {"@BG_CONTENT@", "#fafafa"}, {"@BG_MUTED@", "#f8f9fa"},
        {"@BG_HOVER@", "#eaeaea"}, {"@BG_PRESSED@", "#e0e0e0"},
        {"@BG_TRACK@", "#e8e8e8"}, {"@BG_TAB@", "#f0f0f0"},
        {"@BG_DISABLED@", "#d5d7db"}, {"@BG_MSG@", "#edf0f4"},
        {"@BG_GLASS@", "rgba(255, 255, 255, 0.72)"},
        {"@BORDER@", "#e8eaed"}, {"@BORDER_STRONG@", "#dadce0"},
        {"@BORDER_HOVER@", "#bdbdbd"}, {"@BORDER_DASHED@", "#c4c7c5"},
        {"@BORDER_DISABLED@", "#b0b0b0"}, {"@BORDER_LIGHT@", "#f0f1f3"},
        {"@TEXT_SECONDARY@", "#666666"}, {"@TEXT_TERTIARY@", "#888888"},
        {"@TEXT_DISABLED@", "#bbbbbb"}, {"@TEXT_PRIMARY@", "#333333"},
        {"@TEXT_BORDER@", "#e8eaed"},
        {"@PRIMARY_BG@", "#e8f5e9"}, {"@TEXT_ACCENT@", "#1b5e20"},
        {"@BLACK_RGBA_0.06@", "rgba(0, 0, 0, 0.06)"},
        {"@BLACK_RGBA_0.08@", "rgba(0, 0, 0, 0.08)"},
        {"@BLACK_RGBA_0.10@", "rgba(0, 0, 0, 0.10)"},
        {"@BLACK_RGBA_0.13@", "rgba(0, 0, 0, 0.13)"},
        {"@BLACK_RGBA_0.25@", "rgba(0, 0, 0, 0.25)"},
        {"@PRIMARY@", "#2E7D32"},
        {"@PRIMARY_HOVER@", "#256428"},
        {"@PRIMARY_PRESSED@", "#1B5E20"},
        {"@PRIMARY_DARK@", "#1B5E20"},
        {"@PRIMARY_RGBA_12@", "rgba(46, 125, 50, 0.12)"},
        {"@PRIMARY_RGBA_20@", "rgba(46, 125, 50, 0.2)"},
        {"@PRIMARY_RGBA_38@", "rgba(46, 125, 50, 0.38)"},
        {"@TEXT_ON_PRIMARY@", "#ffffff"},
        {"@SUCCESS@", "#10B981"}, {"@INFO@", "#2196F3"},
        {"@WARNING@", "#F57C00"}, {"@DANGER@", "#D32F2F"},
    };
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

void dumpLogos(QWidget *root, const char *tag)
{
    const auto banners = root->findChildren<QLabel *>(QStringLiteral("contentCardBanner"));
    for (QLabel *banner : banners) {
        auto *logo = banner->findChild<QLabel *>(QStringLiteral("contentCardLogo"));
        if (!logo) {
            std::fprintf(stderr, "[%s] banner=%dx%d no-logo\n", tag,
                         banner->width(), banner->height());
            continue;
        }
        const QRect lg = logo->geometry();
        const int bw = banner->width(), bh = banner->height();
        const bool centered = lg.width() > 60
            && qAbs(lg.x() - (bw - lg.width()) / 2) <= 2
            && qAbs(lg.y() - (bh - lg.height()) / 2) <= 2;
        const bool corner = lg.width() <= 52 && lg.x() >= bw - 52 - 14 - 1;
        std::fprintf(stderr, "[%s] banner=%dx%d logo=(%d,%d) %dx%d %s\n", tag, bw, bh,
                     lg.x(), lg.y(), lg.width(), lg.height(),
                     centered ? "CENTERED" : (corner ? "corner" : "OTHER"));
    }
}

void saveShot(QWidget &w, const char *tag, const QString &path)
{
    const bool ok = w.grab().save(path);
    std::fprintf(stderr, "[%s] %s %s\n", ok ? "ok" : "FAIL", tag, qUtf8Printable(path));
}

ModInfo makeCard(const QString &id, const QString &name, const QString &desc)
{
    ModInfo info;
    info.source = QStringLiteral("modrinth");
    info.id = id;
    info.name = name;
    info.chineseName = name;
    info.description = desc;
    return info;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("masonry_badge_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();
    qApp->setStyleSheet(resolveTokens(raw));

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    // 封面回填卡的本地图源
    const QString coverPath = QStringLiteral("D:/BlockBox/_out/masonry_cover_src.png");
    {
        QPixmap coverSrc(520, 260);
        coverSrc.fill(Qt::transparent);
        QPainter p(&coverSrc);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, 520, 260);
        g.setColorAt(0, QColor("#8ec5fc"));
        g.setColorAt(1, QColor("#e0c3fc"));
        p.fillRect(coverSrc.rect(), g);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 170));
        p.drawEllipse(QPoint(140, 90), 55, 55);
        p.drawEllipse(QPoint(380, 170), 80, 80);
        p.end();
        coverSrc.save(coverPath);
    }

    QWidget win;
    win.setWindowTitle(QStringLiteral("masonry badge probe"));
    win.resize(1180, 460);
    auto *layout = new QHBoxLayout(&win);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(20);

    // A：本地图标、无封面 → 图标居中放大
    ModInfo a = makeCard(QStringLiteral("probe_local_icon"), QStringLiteral("本地图标卡"),
                         QStringLiteral("拿不到封面、只有图标：图标在封面区正中放大"));
    a.iconUrl = QStringLiteral(":/Images/Block/Grass_Block.png");
    layout->addWidget(MasonryContentCard::build(a, &win));

    // B：无图标、无封面 → 首字母徽标同样居中放大
    ModInfo b = makeCard(QStringLiteral("probe_letter"), QStringLiteral("铁傀儡工艺"),
                         QStringLiteral("拿不到封面也拿不到图标：首字母徽标居中"));
    layout->addWidget(MasonryContentCard::build(b, &win));

    // C：封面稍后回填（本地 file:// 图源）→ 先居中，回填成功后收回右下角
    ModInfo c = makeCard(QStringLiteral("probe_cover"), QStringLiteral("封面回填卡"),
                         QStringLiteral("封面加载成功后徽标收回右下角半压"));
    c.coverUrl = QStringLiteral("file:///D:/BlockBox/_out/masonry_cover_src.png");
    layout->addWidget(MasonryContentCard::build(c, &win));

    // D：CurseForge 不加载真实图标 → 字母徽标居中
    ModInfo d = makeCard(QStringLiteral("probe_cf"), QStringLiteral("CF 卡片"),
                         QStringLiteral("CurseForge 图标风格不统一，保持渐变字母"));
    d.source = QStringLiteral("curseforge");
    layout->addWidget(MasonryContentCard::build(d, &win));

    layout->addStretch();
    win.show();
    layout->activate();   // 只做布局激活，不过事件循环（封面 file:// 回复仍在队列里）

    // 封面回填前：先抓一帧（file:// 回复还没进事件循环）
    dumpLogos(&win, "before");
    saveShot(win, "before", QStringLiteral("D:/BlockBox/_out/masonry_badge_before.png"));

    QTimer::singleShot(1200, &win, [&]() {
        dumpLogos(&win, "after ");
        saveShot(win, "after", QStringLiteral("D:/BlockBox/_out/masonry_badge_after.png"));
        app.quit();
    });

    return app.exec();
}
