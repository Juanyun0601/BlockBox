/**
 * @file   combo_look_probe.cpp
 * @brief  下拉框外观核对探针（亮色 / 深色各一张）。
 *
 * 只从真实 styles/style.qss 里挑出与 QComboBox / 存档地图工具栏相关的规则块，
 * token 按 ThemeManager 的亮色 / 深色取值解析，然后并排渲染：
 *   参考  —— worldMapToolCombo（存档地图工具栏里的那枚）
 *   默认  —— 全应用默认 QComboBox，含各页面常见定高（26 / 28 / 32）
 *   紧凑  —— filterCombo 这类筛选栏变体
 *   状态  —— 悬停抬起、禁用
 *
 * 输出：D:/BlockBox/_out/combo_look_<theme>.png
 *       D:/BlockBox/_out/combo_look_<theme>_states.png
 *       D:/BlockBox/_out/combo_look_<theme>_popup.png
 * 退出码：0 = 正常渲染
 */
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

namespace {

// 与 ThemeManager::buildTokenTable() 对齐的精简 token 表（只登记本探针用得到的）
QMap<QString, QString> tokenTable(bool dark)
{
    QMap<QString, QString> T;
    const QColor pc(QStringLiteral("#2E7D32")); // 默认主色调

    if (!dark) {
        T["@BG_BASE@"]        = "#f5f5f5";
        T["@BG_CARD@"]        = "#ffffff";
        T["@BG_CONTENT@"]     = "#fafafa";
        T["@BG_MUTED@"]       = "#f8f9fa";
        T["@BG_HOVER@"]       = "#eaeaea";
        T["@BG_PRESSED@"]     = "#e0e0e0";
        T["@BG_TRACK@"]       = "#e8e8e8";
        T["@BG_TAB@"]         = "#f0f0f0";
        T["@BG_DISABLED@"]    = "#d5d7db";
        T["@BG_MSG@"]         = "#edf0f4";
        T["@BG_GLASS@"]       = "rgba(255, 255, 255, 0.65)";
        T["@BORDER@"]         = "#e8eaed";
        T["@BORDER_STRONG@"]  = "#dadce0";
        T["@BORDER_HOVER@"]   = "#bdbdbd";
        T["@BORDER_DASHED@"]  = "#c4c7c5";
        T["@BORDER_DISABLED@"] = "#b0b0b0";
        T["@BORDER_LIGHT@"]   = "#f0f1f3";
        T["@TEXT_PRIMARY@"]   = "#333333";
        T["@TEXT_SECONDARY@"] = "#666666";
        T["@TEXT_TERTIARY@"]  = "#888888";
        T["@TEXT_DISABLED@"]  = "#bbbbbb";
        T["@TEXT_BORDER@"]    = "#e8e8e8";
        T["@PRIMARY_LIGHT@"]  = pc.lighter(150).name();
        T["@PRIMARY_BG@"]     = pc.lighter(180).name();
        T["@PRIMARY_BG_SOFT@"] = pc.lighter(190).name();
        T["@TEXT_ACCENT@"]    = pc.darker(140).name();
        T["@ACCENT_TEXT@"]    = pc.name();
    } else {
        T["@BG_BASE@"]        = "#1a1a1a";
        T["@BG_CARD@"]        = "#2d2d2d";
        T["@BG_CONTENT@"]     = "#1f1f1f";
        T["@BG_MUTED@"]       = "#2d2d2d";
        T["@BG_HOVER@"]       = "#3d3d3d";
        T["@BG_PRESSED@"]     = "#4a4a4a";
        T["@BG_TRACK@"]       = "#4a4a4a";
        T["@BG_TAB@"]         = "#252525";
        T["@BG_DISABLED@"]    = "#3d3d3d";
        T["@BG_MSG@"]         = "#1a1a1a";
        T["@BG_GLASS@"]       = "rgba(32, 32, 32, 0.86)";
        T["@BORDER@"]         = "#3a3a3a";
        T["@BORDER_STRONG@"]  = "#3a3a3a";
        T["@BORDER_HOVER@"]   = "#444444";
        T["@BORDER_DASHED@"]  = "#555555";
        T["@BORDER_DISABLED@"] = "#444444";
        T["@BORDER_LIGHT@"]   = "#444444";
        T["@TEXT_PRIMARY@"]   = "#e8e8e8";
        T["@TEXT_SECONDARY@"] = "#b0b0b0";
        T["@TEXT_TERTIARY@"]  = "#808080";
        T["@TEXT_DISABLED@"]  = "#808080";
        T["@TEXT_BORDER@"]    = "#666666";
        T["@PRIMARY_LIGHT@"]  = pc.lighter(150).name();
        T["@PRIMARY_BG@"]     = "#2d4d2d";
        T["@PRIMARY_BG_SOFT@"] = pc.lighter(190).name();
        T["@TEXT_ACCENT@"]    = pc.lighter(120).name();
        T["@ACCENT_TEXT@"]    = pc.lighter(160).name();
    }

    T["@BG_SECONDARY@"]   = T.value("@BG_MUTED@");
    T["@BORDER_DEFAULT@"] = T.value("@BORDER@");
    T["@TEXT_ON_PRIMARY@"] = "#ffffff";
    T["@PRIMARY@"]        = pc.name();
    T["@PRIMARY_HOVER@"]  = pc.darker(110).name();
    T["@PRIMARY_PRESSED@"] = pc.darker(125).name();
    T["@PRIMARY_DARK@"]   = pc.darker(130).name();
    T["@SUCCESS@"]        = dark ? "#10B981" : "#059669";
    T["@DANGER@"]         = "#f44336";
    T["@DANGER_BG@"]      = dark ? "#5d2d2d" : "#ffebee";

    const QList<int> alphas = {8, 10, 12, 15, 18, 20, 26, 30, 31, 38, 40, 46, 51, 56, 64, 76};
    for (int a : alphas) {
        T[QString("@PRIMARY_RGBA_%1@").arg(a)] =
            QString("rgba(%1, %2, %3, %4)").arg(pc.red()).arg(pc.green()).arg(pc.blue()).arg(a);
    }
    T["@PRIMARY_RGBA_0.1@"]  = "rgba(46, 125, 50, 0.1)";
    T["@PRIMARY_RGBA_0.15@"] = "rgba(46, 125, 50, 0.15)";
    T["@PRIMARY_RGBA_0.2@"]  = "rgba(46, 125, 50, 0.2)";

    const double blacks[] = {0.06, 0.08, 0.10, 0.12, 0.13, 0.18, 0.20, 0.25, 0.32};
    for (double b : blacks) {
        T[QString("@BLACK_RGBA_%1@").arg(b, 0, 'g', 2)] =
            QString("rgba(0, 0, 0, %1)").arg(b, 0, 'g', 2);
    }
    const int blacksInt[] = {5, 8, 10, 15, 20, 31, 64, 115};
    for (int b : blacksInt)
        T[QString("@BLACK_RGBA_%1@").arg(b)] = QString("rgba(0, 0, 0, %1)").arg(b);

    return T;
}

QString resolveTokens(QString style, bool dark)
{
    const QMap<QString, QString> table = tokenTable(dark);
    for (auto it = table.constBegin(); it != table.constEnd(); ++it)
        style.replace(it.key(), it.value());

    static const QRegularExpression re(QStringLiteral("@[A-Za-z0-9_.]+@"));
    QSet<QString> missing;
    auto m = re.globalMatch(style);
    while (m.hasNext())
        missing.insert(m.next().captured(0));
    if (!missing.isEmpty() && !dark) {
        QStringList names(missing.constBegin(), missing.constEnd());
        std::fprintf(stderr, "[warn] 未登记 token: %s\n",
                     qUtf8Printable(names.join(QStringLiteral(", "))));
    }
    style.replace(re, QStringLiteral("#ff00ff"));

    style.replace(QStringLiteral(":/Images/"), QStringLiteral("D:/BlockBox/C-BlockBox/Images/"));
    return style;
}

// 只挑出下拉框 / 存档地图工具栏相关的规则块，保持原有顺序（级联不变）
QString extractComboRules(const QString &qss)
{
    static const QRegularExpression comment(
        QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
    QString s = qss;
    s.remove(comment);

    QString out;
    int i = 0;
    while (true) {
        const int lb = s.indexOf(QLatin1Char('{'), i);
        if (lb < 0)
            break;
        const int rb = s.indexOf(QLatin1Char('}'), lb);
        if (rb < 0)
            break;
        const QString sel = s.mid(i, lb - i).trimmed();
        const QString body = s.mid(lb + 1, rb - lb - 1);
        if (sel.contains(QLatin1String("QComboBox")) ||
            sel.contains(QLatin1String("worldMapTool")) ||
            sel.contains(QLatin1String("worldMapGroup")) ||
            sel.contains(QLatin1String("worldMapTitle")))
            out += sel + QStringLiteral(" {") + body + QStringLiteral("}\n");
        i = rb + 1;
    }
    return out;
}

// 探针自己的“页面”与卡片，颜色也走 token，切主题时一起变
QString mockQss()
{
    return QStringLiteral(
        "QFrame#probePage { background-color: @BG_BASE@; }\n"
        "QFrame#probeCard { background-color: @BG_CARD@; border: 1px solid @BORDER@;"
        " border-radius: 14px; }\n"
        "QLabel#probeCaption { color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; }\n"
        "QLabel#probeCardTitle { color: @TEXT_PRIMARY@; font-size: 13px; font-weight: 600;"
        " background: transparent; }\n"
        "QLabel#probeRowTitle { color: @TEXT_PRIMARY@; font-size: 13px; font-weight: 600;"
        " background: transparent; }\n"
        "QLabel#probeRowSub { color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; }\n"
        "QFrame#filterBarMock { background-color: @BG_MUTED@; border: 1px solid @BORDER@;"
        " border-radius: 12px; }\n");
}

QComboBox *makeCombo(QWidget *parent, const QString &objectName, int fixedHeight,
                     int minWidth, const QStringList &items)
{
    auto *cb = new QComboBox(parent);
    if (!objectName.isEmpty())
        cb->setObjectName(objectName);
    if (fixedHeight > 0)
        cb->setFixedHeight(fixedHeight);
    cb->setMinimumWidth(minWidth);
    cb->addItems(items);
    cb->setCurrentIndex(0);
    return cb;
}

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("probeCaption"));
    return l;
}

QFrame *card(const QString &title, QWidget *parent)
{
    auto *c = new QFrame(parent);
    c->setObjectName(QStringLiteral("probeCard"));
    auto *lay = new QVBoxLayout(c);
    lay->setContentsMargins(16, 12, 16, 16);
    lay->setSpacing(10);
    auto *t = new QLabel(title, c);
    t->setObjectName(QStringLiteral("probeCardTitle"));
    lay->addWidget(t);
    return c;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // 用法：combo_look_probe [样式文件] [输出后缀]
    const QString qssPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss");
    const QString tag = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    QFile f(qssPath);
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open %s\n", qUtf8Printable(qssPath));
        return 2;
    }
    const QString comboRules = extractComboRules(QString::fromUtf8(f.readAll()));
    f.close();
    QDir().mkpath(QStringLiteral("D:/BlockBox/_out"));

    const QStringList sample = {QStringLiteral("地表方块"), QStringLiteral("高度"),
                                QStringLiteral("全部"), QStringLiteral("10 ~ 100 MB")};

    QFrame win;
    win.setObjectName(QStringLiteral("probePage"));
    auto *root = new QVBoxLayout(&win);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);

    QComboBox *hoverCombo = nullptr;
    QComboBox *disabledCombo = nullptr;
    QComboBox *popupCombo = nullptr;
    QComboBox *popupCombo2 = nullptr;

    // ── 1. 参考：存档地图工具栏 ──
    {
        auto *bar = new QFrame(&win);
        bar->setObjectName(QStringLiteral("worldMapToolbar"));
        auto *barLay = new QHBoxLayout(bar);
        barLay->setContentsMargins(10, 7, 10, 7);
        barLay->setSpacing(10);
        barLay->addWidget(caption(QStringLiteral("参考 · 存档地图工具栏"), bar));

        auto *group = new QWidget(bar);
        group->setObjectName(QStringLiteral("worldMapToolGroup"));
        group->setAttribute(Qt::WA_StyledBackground, true);
        auto *gl = new QHBoxLayout(group);
        gl->setContentsMargins(5, 4, 5, 4);
        gl->setSpacing(4);
        gl->addWidget(makeCombo(group, QStringLiteral("worldMapToolCombo"), 26, 74, sample));
        gl->addWidget(makeCombo(group, QStringLiteral("worldMapToolCombo"), 26, 74, sample));
        popupCombo = makeCombo(group, QStringLiteral("worldMapToolCombo"), 26, 88, sample);
        gl->addWidget(popupCombo);
        barLay->addWidget(group);

        auto *btn = new QLabel(QStringLiteral(" 网格 "), bar);
        btn->setObjectName(QStringLiteral("worldMapGroupBtn"));
        barLay->addWidget(btn);
        barLay->addStretch(1);
        root->addWidget(bar);
    }

    // ── 2. 默认外观 / 紧凑变体 / 页面常见定高 ──
    {
        auto *c = card(QStringLiteral("默认（13px） / 紧凑变体（12px） / 页面定高 26·28·32"), &win);
        auto *lay = qobject_cast<QVBoxLayout *>(c->layout());
        auto *r1 = new QHBoxLayout();
        auto add = [&](const QString &name, const QString &obj, int h) {
            auto *col = new QVBoxLayout();
            col->addWidget(caption(name, c));
            col->addWidget(makeCombo(c, obj, h, 128, sample));
            col->addStretch(1);
            r1->addLayout(col);
        };
        popupCombo2 = makeCombo(c, QString(), 0, 128, sample);
        {
            auto *col = new QVBoxLayout();
            col->addWidget(caption(QStringLiteral("默认（不限高）"), c));
            col->addWidget(popupCombo2);
            col->addStretch(1);
            r1->addLayout(col);
        }
        add(QStringLiteral("filterCombo 32"), QStringLiteral("filterCombo"), 32);
        add(QStringLiteral("filterCombo 28"), QStringLiteral("filterCombo"), 28);
        add(QStringLiteral("forumSource 26"), QStringLiteral("forumSourceCombo"), 26);
        add(QStringLiteral("serverCombo 32"), QStringLiteral("serverCombo"), 32);
        add(QStringLiteral("globalJava 32"), QStringLiteral("globalJavaCombo"), 32);
        r1->addStretch(1);
        lay->addLayout(r1);
        root->addWidget(c);
    }

    // ── 3. 设置行 / 筛选栏里的实际观感 ──
    {
        auto *c = card(QStringLiteral("设置行 / 筛选栏"), &win);
        auto *lay = qobject_cast<QVBoxLayout *>(c->layout());

        auto makeRow = [&](const QString &title, const QString &sub) {
            auto *row = new QHBoxLayout();
            auto *col = new QVBoxLayout();
            auto *t = new QLabel(title, c);
            t->setObjectName(QStringLiteral("probeRowTitle"));
            auto *s = new QLabel(sub, c);
            s->setObjectName(QStringLiteral("probeRowSub"));
            col->addWidget(t);
            if (!sub.isEmpty())
                col->addWidget(s);
            row->addLayout(col);
            row->addStretch(1);
            return row;
        };

        auto *r1 = makeRow(QStringLiteral("Java 路径"),
                           QStringLiteral("选择实例运行用的 Java"));
        r1->addWidget(makeCombo(c, QString(), 32, 220, sample));
        lay->addLayout(r1);

        auto *r2 = makeRow(QStringLiteral("内存模式"),
                           QStringLiteral("自动分配或手动指定"));
        hoverCombo = makeCombo(c, QStringLiteral("filterCombo"), 32, 220, sample);
        r2->addWidget(hoverCombo);
        lay->addLayout(r2);

        auto *r3 = makeRow(QStringLiteral("输入框旁边 / 禁用态"), QString());
        auto *le = new QLineEdit(c);
        le->setText(QStringLiteral("D:/Game/.minecraft"));
        le->setFixedHeight(32);
        le->setMinimumWidth(200);
        r3->addWidget(le);
        disabledCombo = makeCombo(c, QString(), 32, 140, sample);
        disabledCombo->setEnabled(false);
        r3->addWidget(disabledCombo);
        lay->addLayout(r3);

        auto *bar = new QFrame(c);
        bar->setObjectName(QStringLiteral("filterBarMock"));
        auto *bl = new QHBoxLayout(bar);
        bl->setContentsMargins(10, 8, 10, 8);
        bl->setSpacing(8);
        bl->addWidget(makeCombo(bar, QStringLiteral("filterCombo"), 32, 108, sample));
        bl->addWidget(makeCombo(bar, QStringLiteral("categoryCombo"), 32, 108, sample));
        bl->addWidget(makeCombo(bar, QStringLiteral("sortCombo"), 32, 108, sample));
        bl->addStretch(1);
        lay->addWidget(bar);

        root->addWidget(c);
    }

    win.resize(1060, 540);

    for (int pass = 0; pass < 2; ++pass) {
        const bool dark = pass == 1;
        const QString theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
        qApp->setStyleSheet(resolveTokens(comboRules + mockQss(), dark));

        win.show();
        for (int i = 0; i < 8; ++i)
            app.processEvents(QEventLoop::AllEvents, 40);
        if (QWidget *fw = QApplication::focusWidget())
            fw->clearFocus();
        app.processEvents(QEventLoop::AllEvents, 40);

        const QString shot =
            QStringLiteral("D:/BlockBox/_out/combo_look_%1%2.png").arg(theme, tag);
        win.grab().save(shot);
        std::fprintf(stderr, "[ok] %s\n", qUtf8Printable(shot));

        // 悬停态：给 Enter 事件，让 :hover 生效后单独截一枚
        if (hoverCombo) {
            QEvent enter(QEvent::Enter);
            QApplication::sendEvent(hoverCombo, &enter);
            app.processEvents(QEventLoop::AllEvents, 40);
            hoverCombo->grab().save(
                QStringLiteral("D:/BlockBox/_out/combo_look_%1%2_hover.png").arg(theme, tag));
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(hoverCombo, &leave);
            app.processEvents(QEventLoop::AllEvents, 40);
        }

        // 仅聚焦（不展开）：确认焦点边框画在哪
        if (popupCombo) {
            popupCombo->setFocus(Qt::TabFocusReason);
            app.processEvents(QEventLoop::AllEvents, 60);
            popupCombo->grab().save(
                QStringLiteral("D:/BlockBox/_out/combo_look_%1%2_focus.png").arg(theme, tag));
        }

        // 展开态：抓下拉列表本体
        if (popupCombo) {
            popupCombo->showPopup();
            for (int i = 0; i < 6; ++i)
                app.processEvents(QEventLoop::AllEvents, 40);
            popupCombo->grab().save(
                QStringLiteral("D:/BlockBox/_out/combo_look_%1%2_open.png").arg(theme, tag));
            if (popupCombo2) {
                popupCombo2->showPopup();
                for (int i = 0; i < 6; ++i)
                    app.processEvents(QEventLoop::AllEvents, 40);
                popupCombo2->grab().save(
                    QStringLiteral("D:/BlockBox/_out/combo_look_%1%2_open2.png").arg(theme, tag));
                popupCombo2->hidePopup();
                app.processEvents(QEventLoop::AllEvents, 40);
            }
            if (QAbstractItemView *v = popupCombo->view()) {
                const QString p = QStringLiteral("D:/BlockBox/_out/combo_look_%1%2_popup.png")
                                      .arg(theme, tag);
                v->grab().save(p);
                std::fprintf(stderr, "[ok] %s (%dx%d)\n", qUtf8Printable(p),
                             v->width(), v->height());
            }
            popupCombo->hidePopup();
            app.processEvents(QEventLoop::AllEvents, 40);
        }
    }

    return 0;
}
