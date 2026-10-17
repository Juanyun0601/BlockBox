/**
 * @file   combo_style_probe.cpp
 * @brief  下拉框样式统一性视觉探针：同一行渲染 filterCombo / searchSortCombo /
 *         categoryCombo（真实 style.qss + token 解析），断言三者背景色、边框色、
 *         宽度一致。用于坐实/回归 QSS 中 searchSortCombo 缺伪元素后缀的 bug。
 *
 * 输出：D:/BlockBox/_out/combo_style_probe.png（三下拉合影）
 *       D:/BlockBox/_out/combo_style_<name>.png（单体）
 * 退出码：0 = 一致；1 = 不一致（打印各色值/宽度）
 */
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QPixmap>
#include <QRegularExpression>
#include <QVector>
#include <QWidget>

#include <cstdio>

namespace {

// 与 ThemeManager::buildTokenTable 同步的浅色 token 表（探针用精简版）
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
        {"@BG_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.42)"},
        {"@BORDER_GLASS_TASKBAR@", "rgba(255, 255, 255, 0.55)"},
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
        {"@PRIMARY_RGBA_18@", "rgba(46, 125, 50, 0.18)"},
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

int g_fail = 0;
void check(bool ok, const char *msg)
{
    std::fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok)
        ++g_fail;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("combo_style_probe"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly))
    {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    f.close();
    qApp->setStyleSheet(resolveTokens(raw));

    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    // 同一行四个下拉：前三个 objectName 不同、其余属性一致（高32）；
    // 第四个 filterCombo 定高 28，用于验证「页面代码定高 28 vs 32 是否有视觉差异」
    // （QSS min-height 若压过 setFixedHeight，则两者渲染等高，页面无需改）
    QWidget win;
    win.setWindowTitle(QStringLiteral("combo style probe"));
    auto *lay = new QHBoxLayout(&win);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(20);

    const char *names[] = {"filterCombo", "searchSortCombo", "categoryCombo", "filterCombo"};
    const int fixedH[] = {32, 32, 32, 28};
    QComboBox *combos[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 4; ++i)
    {
        auto *cb = new QComboBox(&win);
        cb->setObjectName(QLatin1String(names[i]));
        cb->setFixedHeight(fixedH[i]);
        cb->setMinimumWidth(104);
        cb->addItem(QStringLiteral("名称 A→Z"));
        cb->addItem(QStringLiteral("10 ~ 100 MB"));
        cb->setCurrentIndex(0);
        lay->addWidget(cb);
        combos[i] = cb;
    }
    win.resize(740, 76);
    win.show();
    for (int i = 0; i < 6; ++i)
        app.processEvents(QEventLoop::AllEvents, 40);

    // 清焦点：窗口激活后第一个可聚焦控件会带 :focus 主色边框，采样前移除
    if (QWidget *fw = QApplication::focusWidget())
        fw->clearFocus();
    for (int i = 0; i < 4; ++i)
        app.processEvents(QEventLoop::AllEvents, 40);

    // 单体截图 + 采样：背景取左上内角(4,4)（1px 边框内、5px padding 里），
    // 边框取顶边中点
    struct Sample { QColor bg; QColor border; int w; int h; };
    Sample s[4];
    const QString labels[4] = {
        QStringLiteral("filter"), QStringLiteral("searchsort"),
        QStringLiteral("category"), QStringLiteral("filter28") };
    for (int i = 0; i < 4; ++i)
    {
        const QPixmap pm = combos[i]->grab();
        pm.save(QStringLiteral("D:/BlockBox/_out/combo_style_%1.png").arg(labels[i]));
        const QImage img = pm.toImage();
        s[i].bg = img.pixelColor(4, 4);
        s[i].border = img.pixelColor(img.width() / 2, 0);
        s[i].w = img.width();
        s[i].h = img.height();
        std::fprintf(stderr,
                     "[%s] bg=%s border=%s size=%dx%d\n",
                     qUtf8Printable(labels[i]),
                     qUtf8Printable(s[i].bg.name()),
                     qUtf8Printable(s[i].border.name()),
                     s[i].w, s[i].h);
    }

    const QPixmap groupShot = win.grab();
    groupShot.save(QStringLiteral("D:/BlockBox/_out/combo_style_probe.png"));

    // 断言：三者一致
    auto close = [](const QColor &a, const QColor &b) {
        return qAbs(a.red() - b.red()) <= 2 && qAbs(a.green() - b.green()) <= 2
            && qAbs(a.blue() - b.blue()) <= 2;
    };
    check(s[1].bg == s[0].bg,
          "searchSortCombo 背景色 == filterCombo 背景色");
    check(s[2].bg == s[0].bg,
          "categoryCombo 背景色 == filterCombo 背景色");
    check(s[1].border == s[0].border,
          "searchSortCombo 边框色 == filterCombo 边框色");
    check(s[2].border == s[0].border,
          "categoryCombo 边框色 == filterCombo 边框色");
    check(qAbs(s[1].w - s[0].w) <= 2, "searchSortCombo 宽度 == filterCombo 宽度（±2px）");
    check(qAbs(s[2].w - s[0].w) <= 2, "categoryCombo 宽度 == filterCombo 宽度（±2px）");
    check(s[0].h == s[1].h && s[1].h == s[2].h,
          "三种 objectName 渲染高度一致");
    // 页面代码定高 28 vs 32：若 QSS min-height 压过 setFixedHeight，则视觉等高（页面无需改）
    check(s[3].h == s[0].h,
          "filterCombo 定高28 与 定高32 渲染等高（QSS 主导行高）");
    // 背景不应是主色浅底（item:selected 误吃到 widget 的特征）
    check(!close(s[1].bg, QColor(QStringLiteral("#e8f5e9"))),
          "searchSortCombo 背景不是 PRIMARY_BG（未误吃 item:selected）");
    check(!close(s[1].bg, QColor(QStringLiteral("#f8f9fa"))),
          "searchSortCombo 背景不是 BG_MUTED（未误吃 item:hover）");

    std::fprintf(stderr, "\n======== combo_style: %s ========\n",
                 g_fail == 0 ? "ALL PASS" : "HAS FAIL");
    return g_fail == 0 ? 0 : 1;
}
