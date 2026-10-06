/**
 * @file   view_switch_probe.cpp
 * @brief  ContentViewSwitch（列表 / 瀑布流视图切换）视觉探针
 *
 * 加载真实 style.qss（token 解析后），渲染真实 ContentViewSwitch：
 *  - 像素断言：轨道底色、选中药丸主色、选中图标反白、未选中图标灰度
 *  - setViewMode(List) 后验证药丸随选中段移动
 *  - 桩主题 light→dark 触发 themeChanged，验证图标重绘
 *  - 捕获 "Could not parse" 样式表解析告警
 * 截图输出：D:/BlockBox/_out/view_switch_probe_{masonry,list,dark}.png
 */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdio>

#include "components/ContentViewSwitch.h"
#include "utils/ThemeManager.h"

namespace {

int g_parseWarnings = 0;
int g_fail = 0;

void msgHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (msg.contains(QLatin1String("Could not parse")))
        ++g_parseWarnings;
    std::fprintf(stderr, "[qt ] %s\n", msg.toLocal8Bit().constData());
}

void expect(bool ok, const QString &what)
{
    std::fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", qUtf8Printable(what));
    if (!ok)
        ++g_fail;
}

// 与 ThemeManager::buildTokenTable 同族的 token 表（探针用精简版，dark=true 覆盖深色值）
QString resolveTokens(QString style, bool dark)
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
        {"@TEXT_BORDER@", "#e8e8e8"},
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
    // 深色先替换：同名令牌只命中一次，后到的浅色表自动跳过
    if (dark) {
        const QVector<KV> d = {
            {"@BG_BASE@", "#1a1a1a"}, {"@BG_CARD@", "#2d2d2d"},
            {"@BG_CONTENT@", "#1f1f1f"}, {"@BG_MUTED@", "#2d2d2d"},
            {"@BG_HOVER@", "#3d3d3d"}, {"@BG_PRESSED@", "#4a4a4a"},
            {"@BG_TRACK@", "#4a4a4a"}, {"@BG_TAB@", "#252525"},
            {"@BG_GLASS@", "rgba(45, 45, 45, 0.75)"},
            {"@BORDER@", "#3a3a3a"}, {"@BORDER_STRONG@", "#3a3a3a"},
            {"@BORDER_LIGHT@", "#444444"},
            {"@TEXT_SECONDARY@", "#b0b0b0"}, {"@TEXT_TERTIARY@", "#808080"},
            {"@TEXT_PRIMARY@", "#e0e0e0"},
        };
        for (const auto &kv : d)
            style.replace(QLatin1String(kv.k), QLatin1String(kv.v));
    }
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    // 兜底：剩余 token 置灰，避免 QSS 解析告警
    static const QRegularExpression re(QStringLiteral("@([A-Za-z0-9_.\\-]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

QColor sampleAt(const QImage &img, int x, int y)
{
    const qreal dpr = img.devicePixelRatio();
    return img.pixelColor(qRound(x * dpr), qRound(y * dpr));
}

bool near(const QColor &a, const QColor &b, int tol = 8)
{
    return qAbs(a.red() - b.red()) <= tol
        && qAbs(a.green() - b.green()) <= tol
        && qAbs(a.blue() - b.blue()) <= tol;
}

int countNear(const QImage &img, const QRect &r, const QColor &c, int tol)
{
    const qreal dpr = img.devicePixelRatio();
    const int x0 = qRound(r.left() * dpr), x1 = qRound((r.right() + 1) * dpr);
    const int y0 = qRound(r.top() * dpr), y1 = qRound((r.bottom() + 1) * dpr);
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (near(img.pixelColor(x, y), c, tol))
                ++n;
    return n;
}

// 诊断：打印区域内的主色直方图（前 6 种）
void dumpColors(const QImage &img, const QRect &r, const char *tag)
{
    const qreal dpr = img.devicePixelRatio();
    const int x0 = qRound(r.left() * dpr), x1 = qRound((r.right() + 1) * dpr);
    const int y0 = qRound(r.top() * dpr), y1 = qRound((r.bottom() + 1) * dpr);
    QHash<QRgb, int> hist;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            ++hist[img.pixelColor(x, y).rgba()];
    QList<QPair<int, QRgb>> sorted;
    for (auto it = hist.cbegin(); it != hist.cend(); ++it)
        sorted.append(qMakePair(it.value(), it.key()));
    std::sort(sorted.begin(), sorted.end(), std::greater<QPair<int, QRgb>>());
    std::fprintf(stderr, "[hist] %s rect=%d,%d %dx%d dpr=%.2f:",
                 tag, r.x(), r.y(), r.width(), r.height(), double(dpr));
    for (int i = 0; i < qMin(6, sorted.size()); ++i) {
        const QColor c = QColor::fromRgba(sorted[i].second);
        std::fprintf(stderr, " #%02X%02X%02X x%d",
                     c.red(), c.green(), c.blue(), sorted[i].first);
    }
    std::fprintf(stderr, "\n");
}

QPushButton *roleBtn(const ContentViewSwitch *sw, const char *role)
{
    for (QPushButton *b : sw->findChildren<QPushButton *>())
        if (b->property("viewRole") == QLatin1String(role))
            return b;
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    qInstallMessageHandler(msgHandler);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBox"));
    QCoreApplication::setApplicationName(QStringLiteral("view_switch_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();

    qApp->setStyleSheet(resolveTokens(raw, false)
                        + QStringLiteral("\nQWidget#probeCard { background-color: #ffffff; }"));

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    QWidget card;
    card.setObjectName(QStringLiteral("probeCard"));
    QVBoxLayout lay(&card);
    lay.setContentsMargins(16, 16, 16, 16);
    ContentViewSwitch *sw = new ContentViewSwitch(&card);
    sw->setViewMode(ContentViewSwitch::Masonry);
    lay.addWidget(sw, 0, Qt::AlignLeft);
    card.resize(220, 80);
    card.show();
    app.processEvents();

    if (auto *hl = qobject_cast<QHBoxLayout *>(sw->layout())) {
        const QMargins m = hl->contentsMargins();
        std::fprintf(stderr, "[layout] margins=%d/%d/%d/%d spacing=%d items=%d\n",
                     m.left(), m.top(), m.right(), m.bottom(),
                     hl->spacing(), hl->count());
        for (int i = 0; i < hl->count(); ++i) {
            const QRect g = hl->itemAt(i)->geometry();
            std::fprintf(stderr, "[item %d] %d,%d %dx%d\n",
                         i, g.x(), g.y(), g.width(), g.height());
        }
    }

    const int warnsAfterLoad = g_parseWarnings;
    expect(warnsAfterLoad == 0,
           QStringLiteral("style.qss 解析无告警 (%1)").arg(warnsAfterLoad));

    QPushButton *listBtn = roleBtn(sw, "list");
    QPushButton *masBtn = roleBtn(sw, "masonry");
    expect(listBtn && masBtn, QStringLiteral("找到 list / masonry 两个按钮"));
    if (!listBtn || !masBtn)
        return 1;

    const QRect listR = listBtn->geometry();
    const QRect masR = masBtn->geometry();
    const QRect swR = sw->rect();
    auto rectStr = [](const QRect &r) {
        return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y())
            .arg(r.width()).arg(r.height());
    };
    std::fprintf(stderr, "[geo] widget=%dx%d contents=%s list=%s masonry=%s\n",
                 swR.width(), swR.height(),
                 qUtf8Printable(rectStr(sw->contentsRect())),
                 qUtf8Printable(rectStr(listR)),
                 qUtf8Printable(rectStr(masR)));
    expect(swR.contains(listR) && swR.contains(masR),
           QStringLiteral("按钮均在控件矩形内（无裁切）"));

    // ── 亮色 / 瀑布流选中 ────────────────────────────────────────────────
    QImage img = sw->grab().toImage();
    const QColor kTrackLight(0xF8, 0xF9, 0xFA);   // @BG_MUTED@ 亮色
    const QColor kPrimary(0x2E, 0x7D, 0x32);       // @PRIMARY@
    const QColor kIdleLight(0x64, 0x74, 0x8B);     // 未选中图标（亮色）
    const QColor kWhite(0xFF, 0xFF, 0xFF);
    // 图标为 1.8px 描边 + 抗锯齿，覆盖率约 90% 会与底色混合，放宽匹配容差
    const int kIconTol = 24;
    const int gapX = (listR.right() + masR.left()) / 2;
    dumpColors(img, listR, "stage1-list");
    dumpColors(img, masR, "stage1-masonry");

    expect(near(sampleAt(img, gapX, swR.center().y()), kTrackLight),
           QStringLiteral("轨道间隙 = BG_MUTED 亮色"));
    expect(near(sampleAt(img, masR.x() + 3, masR.center().y()), kPrimary),
           QStringLiteral("瀑布流选中药丸 = PRIMARY"));
    expect(near(sampleAt(img, listR.x() + 3, listR.center().y()), kTrackLight),
           QStringLiteral("列表未选中 = 透明（透出轨道）"));
    expect(countNear(img, masR, kWhite, kIconTol) > 0,
           QStringLiteral("瀑布流选中图标反白"));
    expect(countNear(img, listR, kIdleLight, kIconTol) > 0,
           QStringLiteral("列表未选中图标 = #64748B"));
    sw->grab().save(QStringLiteral("D:/BlockBox/_out/view_switch_probe_masonry.png"));
    card.grab().save(QStringLiteral("D:/BlockBox/_out/view_switch_probe_card.png"));
    std::fprintf(stderr, "[attr] after stage1: sw styled-bg=%d visible=%d style=%s\n",
                 sw->testAttribute(Qt::WA_StyledBackground), int(sw->isVisible()),
                 sw->style()->metaObject()->className());

    // 嵌套渲染（与真实应用一致的合成路径）：轨道与圆角必须可见
    {
        QImage cimg = card.grab().toImage();
        const QPoint org = sw->mapTo(&card, QPoint(0, 0));
        const QColor nTrack = sampleAt(cimg, org.x() + gapX, org.y() + swR.center().y());
        expect(near(nTrack, kTrackLight),
               QStringLiteral("嵌套渲染轨道 = BG_MUTED (实际 #%1%2%3)")
                   .arg(nTrack.red(), 2, 16)
                   .arg(nTrack.green(), 2, 16)
                   .arg(nTrack.blue(), 2, 16));
        const QRect masOnCard = masR.translated(org);
        auto spanW = [&](int y) {
            int x0 = -1, x1 = -1;
            for (int x = masOnCard.x(); x < masOnCard.x() + masOnCard.width(); ++x) {
                if (near(cimg.pixelColor(x, y), kPrimary, 24)) {
                    if (x0 < 0) x0 = x;
                    x1 = x;
                }
            }
            return (x0 >= 0 && x1 >= x0) ? x1 - x0 + 1 : 0;
        };
        const int topW = spanW(masOnCard.y() + 1);
        const int midW = spanW(masOnCard.y() + masOnCard.height() / 2);
        expect(topW > 0 && topW < midW,
               QStringLiteral("嵌套渲染选中药丸圆角 (top=%1 mid=%2)").arg(topW).arg(midW));
    }

    // ── 切到列表：药丸应移动 ─────────────────────────────────────────────
    sw->setViewMode(ContentViewSwitch::List);
    app.processEvents();
    img = sw->grab().toImage();
    dumpColors(img, listR, "stage2-list");
    dumpColors(img, masR, "stage2-masonry");
    expect(near(sampleAt(img, listR.x() + 3, listR.center().y()), kPrimary),
           QStringLiteral("列表选中药丸 = PRIMARY"));
    expect(near(sampleAt(img, masR.x() + 3, masR.center().y()), kTrackLight),
           QStringLiteral("瀑布流恢复未选中"));
    expect(countNear(img, listR, kWhite, kIconTol) > 0,
           QStringLiteral("列表选中图标反白"));
    expect(countNear(img, masR, kIdleLight, kIconTol) > 0,
           QStringLiteral("瀑布流未选中图标 = #64748B"));
    sw->grab().save(QStringLiteral("D:/BlockBox/_out/view_switch_probe_list.png"));

    // ── 深色主题：themeChanged → 图标重绘 ───────────────────────────────
    const int warnsBeforeDark = g_parseWarnings;
    qApp->setStyleSheet(resolveTokens(raw, true)
                        + QStringLiteral("\nQWidget#probeCard { background-color: #2d2d2d; }"));
    ThemeManager::instance()->setThemeForProbe(ThemeManager::DarkTheme);
    app.processEvents();
    expect(g_parseWarnings == warnsBeforeDark,
           QStringLiteral("深色样式表解析无告警"));

    img = sw->grab().toImage();
    dumpColors(img, listR, "stage3-dark-list");
    dumpColors(img, masR, "stage3-dark-masonry");
    const QColor kTrackDark(0x2D, 0x2D, 0x2D);     // @BG_MUTED@ 深色
    const QColor kIdleDark(0x9C, 0xA3, 0xAF);      // 未选中图标（深色）
    expect(near(sampleAt(img, gapX, swR.center().y()), kTrackDark),
           QStringLiteral("深色轨道 = BG_MUTED 深色"));
    expect(near(sampleAt(img, listR.x() + 3, listR.center().y()), kPrimary),
           QStringLiteral("深色选中药丸 = PRIMARY"));
    expect(countNear(img, listR, kWhite, kIconTol) > 0,
           QStringLiteral("深色选中图标仍反白"));
    expect(countNear(img, masR, kIdleDark, kIconTol) > 0,
           QStringLiteral("themeChanged 后未选中图标 = #9CA3AF"));
    sw->grab().save(QStringLiteral("D:/BlockBox/_out/view_switch_probe_dark.png"));


    std::fprintf(stderr, "[done] failures=%d parseWarnings=%d\n",
                 g_fail, g_parseWarnings);
    return g_fail == 0 ? 0 : 1;
}
