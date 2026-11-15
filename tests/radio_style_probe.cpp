/**
 * @file   radio_style_probe.cpp
 * @brief  单选框（QRadioButton）样式探针：加载真实 style.qss，渲染各种状态并截图
 *
 * 用法：
 *   qmake radio_style_probe.pro && mingw32-make && ./radio_style_probe.exe [--dark]
 *   运行后输出 _out/radio_style_light.png / _out/radio_style_dark.png
 *
 * 覆盖状态：未选中 / 选中 / 禁用 / 选中禁用 / 键盘焦点 / 分组框中 / 横向排列 / 长文本。
 * 仅用于肉眼核对样式，不做像素断言。
 */

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QRegularExpression>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <cstdio>

namespace {

struct Palette {
    const char *bgBase;
    const char *bgCard;
    const char *bgMuted;
    const char *bgHover;
    const char *border;
    const char *borderHover;
    const char *borderDisabled;
    const char *textPrimary;
    const char *textSecondary;
    const char *textDisabled;
    const char *primaryBg;
    const char *textAccent;
};

QString resolveTokens(QString style, bool dark)
{
    // 精简版 ThemeManager token 表（默认主题色 #2E7D32），只覆盖控件样式所需
    const QColor pc(QStringLiteral("#2E7D32"));
    const Palette lightPal{
        "#f5f5f5", "#ffffff", "#f8f9fa", "#eaeaea", "#e8eaed", "#bdbdbd",
        "#b0b0b0", "#333333", "#666666", "#bbbbbb", "#e8f5e9", "#1b5e20"};
    const Palette darkPal{
        "#1a1a1a", "#2d2d2d", "#2d2d2d", "#3d3d3d", "#3a3a3a", "#444444",
        "#444444", "#e8e8e8", "#b0b0b0", "#808080", "#2d4d2d", "#66bb6a"};
    const Palette &p = dark ? darkPal : lightPal;

    struct KV { const char *k; QString v; };
    const QVector<KV> tokens = {
        {"@BG_BASE@", p.bgBase},
        {"@BG_CARD@", p.bgCard},
        {"@BG_CONTENT@", p.bgMuted},
        {"@BG_MUTED@", p.bgMuted},
        {"@BG_HOVER@", p.bgHover},
        {"@BG_PRESSED@", p.bgHover},
        {"@BG_TRACK@", p.bgHover},
        {"@BG_TAB@", p.bgMuted},
        {"@BG_DISABLED@", p.bgHover},
        {"@BG_MSG@", p.bgMuted},
        {"@BORDER@", p.border},
        {"@BORDER_STRONG@", p.border},
        {"@BORDER_HOVER@", p.borderHover},
        {"@BORDER_DASHED@", p.borderHover},
        {"@BORDER_DISABLED@", p.borderDisabled},
        {"@BORDER_LIGHT@", p.border},
        {"@TEXT_PRIMARY@", p.textPrimary},
        {"@TEXT_SECONDARY@", p.textSecondary},
        {"@TEXT_TERTIARY@", p.textSecondary},
        {"@TEXT_DISABLED@", p.textDisabled},
        {"@TEXT_ACCENT@", p.textAccent},
        {"@TEXT_ACCENT_DARK@", p.textAccent},
        {"@PRIMARY_TEXT@", p.textAccent},
        {"@PRIMARY@", pc.name()},
        {"@PRIMARY_HOVER@", pc.darker(110).name()},
        {"@PRIMARY_PRESSED@", pc.darker(125).name()},
        {"@PRIMARY_DARK@", pc.darker(130).name()},
        {"@PRIMARY_GRAD1@", pc.lighter(130).name()},
        {"@PRIMARY_GRAD2@", pc.lighter(150).name()},
        {"@PRIMARY_GRAD3@", pc.lighter(140).name()},
        {"@PRIMARY_MEDIUM@", pc.lighter(120).name()},
        {"@PRIMARY_LIGHT@", pc.lighter(150).name()},
        {"@PRIMARY_LIGHTER@", pc.lighter(160).name()},
        {"@PRIMARY_BG@", p.primaryBg},
        {"@PRIMARY_BG_SOFT@", p.primaryBg},
        {"@SECONDARY@", p.textAccent},
        {"@BORDER_ON_ACCENT@", "#ffffff"},
        {"@TEXT_ON_PRIMARY@", "#ffffff"},
        {"@BG_GLASS@", dark ? "rgba(32,32,32,0.86)" : "rgba(255,255,255,0.65)"},
        {"@BG_GLASS_TASKBAR@", dark ? "rgba(28,28,30,0.48)" : "rgba(255,255,255,0.42)"},
        {"@BORDER_GLASS_TASKBAR@", dark ? "rgba(255,255,255,0.10)" : "rgba(255,255,255,0.55)"},
        {"@BG_CARD_GLASS@", dark ? "rgba(45,45,45,0.65)" : "rgba(255,255,255,0.65)"},
        {"@BG_CONTENT_GLASS@", dark ? "rgba(31,31,31,0.65)" : "rgba(250,250,250,0.65)"},
        {"@SUCCESS@", "#10B981"},
        {"@INFO@", "#2196F3"},
        {"@LINK@", pc.name()},
        {"@LINK_HOVER@", pc.darker(110).name()},
    };

    for (const KV &kv : tokens)
        style.replace(QLatin1String(kv.k), kv.v);

    for (int alpha : {8, 10, 12, 15, 18, 20, 26, 30, 31, 38, 40, 46, 51, 56, 64, 76, 89, 102})
        style.replace(QStringLiteral("@PRIMARY_RGBA_%1@").arg(alpha),
                      QStringLiteral("rgba(%1,%2,%3,%4)")
                          .arg(pc.red()).arg(pc.green()).arg(pc.blue())
                          .arg(alpha / 255.0, 0, 'f', 3));

    // 其余未登记 token 统一灰色，避免 QSS 解析告警
    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    style.replace(re, QStringLiteral("#8a8a8a"));
    return style;
}

QLabel *heading(const QString &text, QWidget *parent, bool dark)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 700; color: %1;")
                             .arg(dark ? "#cfcfcf" : "#555555"));
    return label;
}

QWidget *buildPanel(QWidget *parent, bool dark)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("probePanel"));
    panel->setStyleSheet(QStringLiteral(
        "QFrame#probePanel { background: %1; border: 1px solid %2; border-radius: 12px; }")
        .arg(dark ? "#2d2d2d" : "#ffffff", dark ? "#3a3a3a" : "#e8eaed"));

    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(20, 16, 20, 18);
    lay->setSpacing(6);

    lay->addWidget(heading(QStringLiteral("举报分类 *"), panel, dark));
    auto *hint = new QLabel(QStringLiteral("选一个最贴切的分类，管理员会优先按分类处理。"), panel);
    hint->setStyleSheet(QStringLiteral("font-size: 12px; color: %1;")
                            .arg(dark ? "#9a9a9a" : "#8a8a8a"));
    lay->addWidget(hint);

    const QStringList items = {
        QStringLiteral("水贴 / 无意义内容"),
        QStringLiteral("广告 / 推广（当前选中）"),
        QStringLiteral("人身攻击 / 引战"),
        QStringLiteral("其他（请在下方说明）"),
    };
    QRadioButton *checkedItem = nullptr;
    for (int i = 0; i < items.size(); ++i) {
        auto *rb = new QRadioButton(items.at(i), panel);
        rb->setCursor(Qt::PointingHandCursor);
        lay->addWidget(rb);
        if (i == 1)
            checkedItem = rb;
    }

    // 同一父控件内的 QRadioButton 会自动互斥，分组演示时各自套一层容器
    auto *disabledHost = new QWidget(panel);
    auto *disabledRow = new QHBoxLayout(disabledHost);
    disabledRow->setContentsMargins(0, 0, 0, 0);
    auto *d1 = new QRadioButton(QStringLiteral("禁用的未选中项"), disabledHost);
    auto *d2 = new QRadioButton(QStringLiteral("禁用的选中项"), disabledHost);
    d1->setEnabled(false);
    d2->setEnabled(false);
    d2->setChecked(true);
    disabledRow->addWidget(d1);
    disabledRow->addSpacing(18);
    disabledRow->addWidget(d2);
    disabledRow->addStretch(1);
    lay->addSpacing(4);
    lay->addWidget(disabledHost);

    auto *focusHost = new QWidget(panel);
    auto *focusRow = new QHBoxLayout(focusHost);
    focusRow->setContentsMargins(0, 0, 0, 0);
    focusRow->addWidget(heading(QStringLiteral("键盘焦点"), focusHost, dark));
    auto *f = new QRadioButton(QStringLiteral("按 Tab 选中我"), focusHost);
    f->setChecked(true);
    f->setFocus();
    focusRow->addWidget(f);
    focusRow->addStretch(1);
    lay->addWidget(focusHost);

    auto *longRb = new QRadioButton(
        QStringLiteral("很长的选项文字：这条说明用来检查单选框文字与圆点的间距，以及长文本被截断时的表现"),
        panel);
    lay->addSpacing(4);
    lay->addWidget(longRb);

    // 放在最后再确认一次选中项，避免被同一父控件里的其它单选项顶掉
    if (checkedItem)
        checkedItem->setChecked(true);
    return panel;
}

QGroupBox *buildGroup(QWidget *parent, bool dark)
{
    auto *group = new QGroupBox(QStringLiteral("关机模式（系统工具弹窗）"), parent);
    auto *lay = new QVBoxLayout(group);
    lay->setContentsMargins(14, 16, 14, 12);
    lay->setSpacing(4);
    auto *r1 = new QRadioButton(QStringLiteral("关机 (shutdown)"), group);
    auto *r2 = new QRadioButton(QStringLiteral("重启 (restart)"), group);
    auto *r3 = new QRadioButton(QStringLiteral("取消已计划的关机"), group);
    r1->setChecked(true);
    lay->addWidget(r1);
    lay->addWidget(r2);
    lay->addWidget(r3);
    group->setStyleSheet(QStringLiteral(
        "QGroupBox { border: 1px solid %1; border-radius: 8px; margin-top: 10px;"
        " padding-top: 6px; color: %2; font-size: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }")
        .arg(dark ? "#3a3a3a" : "#e8eaed", dark ? "#cfcfcf" : "#555555"));
    return group;
}

QWidget *buildConvertCard(QWidget *parent)
{
    // 与 WorldConvertDialog 的「转换方向」一致：卡片固定白底，单选框自带浅色样式
    const QString themeColor = QStringLiteral("#2E7D32");
    const QString textColor = QStringLiteral("#1c1c1e");
    const QColor themeCol(themeColor);
    const QString radioTint = QStringLiteral("rgba(%1,%2,%3,0.06)")
                                  .arg(themeCol.red()).arg(themeCol.green()).arg(themeCol.blue());
    const QString radioStyle = QStringLiteral(
        "QRadioButton { color: %1; font-size: 13px; spacing: 10px; padding: 3px 6px;"
        " border-radius: 7px; background: transparent; }"
        "QRadioButton:hover { background: %3; }"
        "QRadioButton:checked, QRadioButton:checked:hover { background: %3; }"
        "QRadioButton:focus { color: %2; background: %3; }"
        "QRadioButton:disabled { color: #b0b0b0; background: transparent; }"
        "QRadioButton::indicator { width: 16px; height: 16px; border-radius: 10px;"
        " border: 2px solid #c7c7cc; background: #ffffff; margin: 0; }"
        "QRadioButton::indicator:hover { border-color: %2; }"
        "QRadioButton::indicator:checked, QRadioButton::indicator:checked:hover {"
        " border: 2px solid %2;"
        " background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,"
        " stop:0 %2, stop:0.42 %2, stop:0.48 #ffffff, stop:1 #ffffff); }"
        "QRadioButton::indicator:disabled { border-color: #d5d7db; background: #f2f2f5; }"
        "QRadioButton::indicator:checked:disabled { border-color: #d5d7db;"
        " background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,"
        " stop:0 #b0b0b0, stop:0.42 #b0b0b0, stop:0.48 #f2f2f5, stop:1 #f2f2f5); }")
        .arg(textColor, themeColor, radioTint);

    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("convertProbeCard"));
    card->setStyleSheet(card->styleSheet() + QStringLiteral(
        "QFrame#convertProbeCard { background: #ffffff; border: 1px solid #e3e3e8;"
        " border-radius: 12px; }"));
    auto *lay = new QHBoxLayout(card);
    lay->setContentsMargins(20, 14, 20, 14);
    lay->setSpacing(16);
    auto *title = new QLabel(QStringLiteral("转换方向"), card);
    title->setStyleSheet(QStringLiteral("color: #1c1c1e; font-size: 13px;"));
    lay->addWidget(title);
    auto *r1 = new QRadioButton(QStringLiteral("Java 版 → 基岩版"), card);
    auto *r2 = new QRadioButton(QStringLiteral("基岩版 → Java 版"), card);
    r1->setStyleSheet(radioStyle);
    r2->setStyleSheet(radioStyle);
    r1->setChecked(true);
    lay->addWidget(r1);
    lay->addWidget(r2);
    lay->addStretch(1);
    return card;
}

QWidget *buildHorizontal(QWidget *parent)
{
    auto *panel = new QWidget(parent);
    auto *lay = new QHBoxLayout(panel);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(16);
    auto *r1 = new QRadioButton(QStringLiteral("Java 版 → 基岩版"), panel);
    auto *r2 = new QRadioButton(QStringLiteral("基岩版 → Java 版"), panel);
    r1->setChecked(true);
    lay->addWidget(r1);
    lay->addWidget(r2);
    lay->addStretch(1);
    return panel;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    const bool dark = app.arguments().contains(QStringLiteral("--dark"));

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    qApp->setStyleSheet(resolveTokens(QString::fromUtf8(f.readAll()), dark));

    QWidget win;
    win.setObjectName(QStringLiteral("probeRoot"));
    win.setStyleSheet(QStringLiteral("QWidget#probeRoot { background: %1; }")
                          .arg(dark ? "#1a1a1a" : "#f5f5f5"));
    win.setWindowTitle(QStringLiteral("radio style probe"));
    win.resize(680, 600);
    auto *root = new QVBoxLayout(&win);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);
    root->addWidget(buildPanel(&win, dark));
    root->addWidget(buildHorizontal(&win));
    root->addWidget(buildConvertCard(&win));
    root->addWidget(buildGroup(&win, dark));
    root->addStretch(1);
    win.show();
    app.processEvents();

    const QImage shot = win.grab().toImage();
    const QString out = QStringLiteral("D:/BlockBox/_out/radio_style_%1.png")
                            .arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
    const bool ok = shot.save(out);
    std::fprintf(stderr, "[%s] %s (%dx%d)\n", ok ? "ok" : "FAIL",
                 qUtf8Printable(out), shot.width(), shot.height());

    // 把鼠标停在第三个选项上，补一张 hover 截图
    const auto radios = win.findChildren<QRadioButton *>();
    if (radios.size() > 3) {
        QRadioButton *hovered = radios.at(2);
        hovered->setAttribute(Qt::WA_UnderMouse, true);
        hovered->update();
        app.processEvents();
        const QString hoverOut = QStringLiteral("D:/BlockBox/_out/radio_style_%1_hover.png")
                                     .arg(dark ? QStringLiteral("dark") : QStringLiteral("light"));
        win.grab().toImage().save(hoverOut);
        std::fprintf(stderr, "[ok] %s\n", qUtf8Printable(hoverOut));
    }
    return ok ? 0 : 3;
}
