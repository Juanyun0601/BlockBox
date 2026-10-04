/**
 * @file   ai_input_visual_probe.cpp
 * @brief  AI 助手页底部输入区视觉探针
 *
 * 复刻 AiChatPage::initUI 中输入区域的控件树与 objectName，
 * 加载真实 style.qss（token 解析后），渲染并截图：
 *   ai_input_light.png        浅色 · 默认
 *   ai_input_light_focus.png  浅色 · 输入框聚焦
 *   ai_input_dark.png         深色 · 默认
 *   ai_input_dark_active.png  深色 · 工作区/系统提示词激活态
 */
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <cstdio>

namespace {

// 与 ThemeManager::buildTokenTable 一致的浅/深主题 token 表
QString resolveTokens(QString style, bool dark)
{
    struct KV { const char *k; const char *v; };
    const char *primary = "#2E7D32";

    QVector<KV> t;
    if (!dark) {
        t = {
            {"@BG_BASE@", "#f5f5f5"}, {"@BG_CARD@", "#ffffff"},
            {"@BG_CONTENT@", "#fafafa"}, {"@BG_MUTED@", "#f8f9fa"},
            {"@BG_HOVER@", "#eaeaea"}, {"@BG_PRESSED@", "#e0e0e0"},
            {"@BG_TRACK@", "#e8e8e8"}, {"@BG_TAB@", "#f0f0f0"},
            {"@BG_DISABLED@", "#d5d7db"}, {"@BG_MSG@", "#edf0f4"},
            {"@BORDER@", "#e8eaed"}, {"@BORDER_STRONG@", "#dadce0"},
            {"@BORDER_HOVER@", "#bdbdbd"}, {"@BORDER_DASHED@", "#c4c7c5"},
            {"@BORDER_DISABLED@", "#b0b0b0"}, {"@BORDER_LIGHT@", "#f0f1f3"},
            {"@TEXT_SECONDARY@", "#666666"}, {"@TEXT_TERTIARY@", "#888888"},
            {"@TEXT_DISABLED@", "#bbbbbb"}, {"@TEXT_PRIMARY@", "#333333"},
            {"@PRIMARY_BG@", "#e8f5e9"}, {"@TEXT_ACCENT@", "#1b5e20"},
            {"@BLACK_RGBA_0.06@", "rgba(0, 0, 0, 0.06)"},
            {"@BLACK_RGBA_0.13@", "rgba(0, 0, 0, 0.13)"},
            {"@BLACK_RGBA_0.25@", "rgba(0, 0, 0, 0.25)"},
        };
    } else {
        t = {
            {"@BG_BASE@", "#1a1a1a"}, {"@BG_CARD@", "#2d2d2d"},
            {"@BG_CONTENT@", "#1f1f1f"}, {"@BG_MUTED@", "#2d2d2d"},
            {"@BG_HOVER@", "#3d3d3d"}, {"@BG_PRESSED@", "#4a4a4a"},
            {"@BG_TRACK@", "#4a4a4a"}, {"@BG_TAB@", "#252525"},
            {"@BG_DISABLED@", "#3d3d3d"}, {"@BG_MSG@", "#1a1a1a"},
            {"@BORDER@", "#3a3a3a"}, {"@BORDER_STRONG@", "#3a3a3a"},
            {"@BORDER_HOVER@", "#444444"}, {"@BORDER_DASHED@", "#555555"},
            {"@BORDER_DISABLED@", "#444444"}, {"@BORDER_LIGHT@", "#444444"},
            {"@TEXT_SECONDARY@", "#b0b0b0"}, {"@TEXT_TERTIARY@", "#808080"},
            {"@TEXT_DISABLED@", "#808080"}, {"@TEXT_PRIMARY@", "#e6e6e6"},
            {"@PRIMARY_BG@", "#2d4d2d"}, {"@TEXT_ACCENT@", "#66bb6a"},
            {"@BLACK_RGBA_0.06@", "rgba(255, 255, 255, 0.08)"},
            {"@BLACK_RGBA_0.13@", "rgba(255, 255, 255, 0.15)"},
            {"@BLACK_RGBA_0.25@", "rgba(255, 255, 255, 0.25)"},
        };
    }
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    // 主色族
    const QColor pc{QLatin1String(primary)};
    const auto rgba = [&](const QString &a) {
        return QStringLiteral("rgba(%1, %2, %3, %4)")
            .arg(pc.red()).arg(pc.green()).arg(pc.blue()).arg(a);
    };
    static const struct { const char *k; QString v; } dyn[] = {
        {"@PRIMARY@", pc.name()},
        {"@PRIMARY_HOVER@", pc.darker(110).name()},
        {"@PRIMARY_PRESSED@", pc.darker(125).name()},
        {"@PRIMARY_BG@", dark ? QStringLiteral("#2d4d2d")
                               : pc.lighter(180).name()},
        {"@PRIMARY_RGBA_12@", rgba(QStringLiteral("12"))},
        {"@PRIMARY_RGBA_20@", rgba(QStringLiteral("0.2"))},
        {"@PRIMARY_RGBA_38@", rgba(QStringLiteral("38"))},
        {"@TEXT_ON_PRIMARY@", QStringLiteral("#ffffff")},
        {"@SUCCESS@", QStringLiteral("#10B981")},
        {"@WARNING_TEXT@", QStringLiteral("#e65100")},
        {"@WARNING_TEXT_DARK@", QStringLiteral("#856404")},
    };
    for (const auto &kv : dyn)
        style.replace(QLatin1String(kv.k), kv.v);

    // 兜底：剩余 token 置灰，避免 QSS 解析告警
    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

QWidget *buildBottomArea(QWidget *parent, bool withIcons)
{
    Q_UNUSED(withIcons);
    auto *panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("aiChatInputPanel"));

    auto *inputLayout = new QVBoxLayout(panel);
    inputLayout->setContentsMargins(16, 10, 16, 14);
    inputLayout->setSpacing(8);

    auto *inputBox = new QFrame(panel);
    inputBox->setObjectName(QStringLiteral("aiChatInputBox"));
    auto *ibl = new QVBoxLayout(inputBox);
    ibl->setContentsMargins(12, 8, 8, 8);
    ibl->setSpacing(4);

    auto *edit = new QTextEdit(inputBox);
    edit->setObjectName(QStringLiteral("aiChatInputEdit"));
    edit->setPlaceholderText(QStringLiteral("输入消息，Enter 发送，Shift+Enter 换行"));
    edit->setPlainText(QStringLiteral("帮我看看这个光影包为什么闪白"));
    edit->setMaximumHeight(100);
    edit->setAcceptRichText(false);
    edit->setFrameShape(QFrame::NoFrame);
    ibl->addWidget(edit);

    // 工具行：左侧三选择框 + 弹性 + 右侧五图标（与 AiChatPage::rebuildToolsRows 一致）
    auto *toolsRow = new QHBoxLayout();
    toolsRow->setContentsMargins(0, 0, 0, 0);
    toolsRow->setSpacing(6);

    auto *modelBtn = new QPushButton(QStringLiteral("DeepSeek Chat ▾"), inputBox);
    modelBtn->setObjectName(QStringLiteral("aiChatModelBtn"));
    modelBtn->setCursor(Qt::PointingHandCursor);
    modelBtn->setFlat(true);
    modelBtn->setFixedSize(150, 28);
    toolsRow->addWidget(modelBtn);

    auto *wsBtn = new QPushButton(QStringLiteral("工作区: 不选 ▾"), inputBox);
    wsBtn->setObjectName(QStringLiteral("aiChatWorkspaceBtn"));
    wsBtn->setCursor(Qt::PointingHandCursor);
    wsBtn->setFlat(true);
    wsBtn->setFixedSize(132, 28);
    toolsRow->addWidget(wsBtn);

    auto *effort = new QComboBox(inputBox);
    effort->setObjectName(QStringLiteral("aiChatThinkingEffortCombo"));
    effort->setCursor(Qt::PointingHandCursor);
    effort->addItems({QStringLiteral("思考: 关闭"), QStringLiteral("思考: 低"),
                      QStringLiteral("思考: 中"), QStringLiteral("思考: 高")});
    effort->setCurrentIndex(2);
    effort->setFixedSize(108, 28);
    toolsRow->addWidget(effort);

    auto *perm = new QComboBox(inputBox);
    perm->setObjectName(QStringLiteral("aiChatPermissionCombo"));
    perm->setCursor(Qt::PointingHandCursor);
    perm->addItems({QStringLiteral("权限: 全部确认"), QStringLiteral("权限: 关键确认"),
                    QStringLiteral("权限: 完全访问")});
    perm->setCurrentIndex(0);
    perm->setFixedSize(122, 28);
    toolsRow->addWidget(perm);

    toolsRow->addStretch();

    const char *iconNames[] = {"aiChatToolIconBtn", "aiChatToolIconBtn",
                               "aiChatToolIconBtn", "aiChatToolIconBtn"};
    const char *iconTexts[] = {"引", "优", "词", "技"};
    for (int i = 0; i < 4; ++i) {
        auto *b = new QPushButton(QLatin1String(iconTexts[i]), inputBox);
        b->setObjectName(QLatin1String(iconNames[i]));
        b->setFixedSize(30, 30);
        b->setCursor(Qt::PointingHandCursor);
        toolsRow->addWidget(b);
    }

    auto *sendBtn = new QPushButton(inputBox);
    sendBtn->setObjectName(QStringLiteral("aiChatSendBtn"));
    sendBtn->setFixedSize(32, 32);
    toolsRow->addWidget(sendBtn, 0, Qt::AlignVCenter);
    ibl->addLayout(toolsRow);

    inputLayout->addWidget(inputBox);
    return panel;
}

void setFocusProperty(QWidget *box, bool on)
{
    box->setProperty("focused", on);
    box->style()->unpolish(box);
    box->style()->polish(box);
}

QWidget *makeWindowContent(QWidget *parent, bool dark, bool focused, bool active)
{
    auto *root = new QWidget(parent);
    auto *v = new QVBoxLayout(root);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // 消息区背景（对照层级）
    auto *msgArea = new QScrollArea(root);
    msgArea->setObjectName(QStringLiteral("aiChatMessageArea"));
    msgArea->setWidgetResizable(true);
    auto *msg = new QWidget;
    auto *ml = new QVBoxLayout(msg);
    ml->setContentsMargins(16, 16, 16, 16);
    auto *bubble = new QFrame;
    bubble->setObjectName(QStringLiteral("aiChatAiBubble"));
    bubble->setMinimumHeight(60);
    ml->addWidget(bubble);
    ml->addStretch();
    msgArea->setWidget(msg);
    v->addWidget(msgArea, 1);

    QWidget *panel = buildBottomArea(root, false);
    v->addWidget(panel);

    // 状态变体
    QFrame *box = panel->findChild<QFrame *>(QStringLiteral("aiChatInputBox"));
    if (focused && box)
        setFocusProperty(box, true);

    if (active) {
        // 工作区已选中 + 系统提示词已激活（图标按钮 active 态）
        if (auto *w = panel->findChild<QPushButton *>(QStringLiteral("aiChatWorkspaceBtn"))) {
            w->setText(QStringLiteral("工作区: .minecraft ▾"));
            w->setProperty("active", true);
            w->style()->unpolish(w);
            w->style()->polish(w);
        }
        if (auto *s = panel->findChild<QPushButton *>(QStringLiteral("aiChatToolIconBtn"))) {
            s->setProperty("active", true);
            s->style()->unpolish(s);
            s->style()->polish(s);
        }
    }
    Q_UNUSED(dark);
    return root;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QFile f(QStringLiteral("D:/BlockBox/C-BlockBox/styles/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open style.qss\n");
        return 2;
    }
    const QString raw = QString::fromUtf8(f.readAll());

    struct Shot { const char *name; bool dark; bool focused; bool active; };
    const Shot shots[] = {
        {"ai_input_light.png",  false, false, false},
        {"ai_input_light_focus.png", false, true,  false},
        {"ai_input_dark.png",   true,  false, false},
        {"ai_input_dark_active.png", true, false, true},
    };

    for (const Shot &s : shots) {
        qApp->setStyleSheet(resolveTokens(raw, s.dark));

        QWidget win;
        win.setFixedSize(760, 360);
        auto *rootLayout = new QVBoxLayout(&win);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->addWidget(makeWindowContent(&win, s.dark, s.focused, s.active));

        win.show();
        app.processEvents();
        const QString out = QStringLiteral("D:/BlockBox/_out/") + QLatin1String(s.name);
        const bool ok = win.grab().save(out);
        std::fprintf(stderr, "[%s] %s\n", ok ? "ok" : "FAIL", qUtf8Printable(out));
        win.close();
    }
    QTimer::singleShot(100, &app, &QCoreApplication::quit);
    return app.exec();
}
