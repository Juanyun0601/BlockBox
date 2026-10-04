/**
 * @file   java_empty_fixed_probe.cpp
 * @brief  验证修复后的空状态标签生命周期（与 SettingsJavaPage::refreshJavaList 一致）：
 *   1. 非空初始化：不创建标签 → 切勿出现浮层文字
 *   2. 空状态：按需新建标签并安装为 item widget → 正常显示
 *   3. 空 → 加载 Java：clear() 销毁旧标签 → 文字消失、行出现、无残留
 *   4. 加载 → 再次全空：重新新建标签 → 空状态恢复（无悬空指针崩溃）
 */
#include <QApplication>
#include <QFile>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdio>

namespace {

QString resolveTokens(QString style, bool dark)
{
    struct KV { const char *k; const char *v; };
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
            {"@DANGER@", "#c62828"}, {"@DANGER_BG@", "#ffebee"},
            {"@DANGER_BORDER@", "#ffcdd2"},
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
            {"@DANGER@", "#ff6b6b"}, {"@DANGER_BG@", "#5d2d2d"},
            {"@DANGER_BORDER@", "#8d4d4d"},
        };
    }
    for (const auto &kv : t)
        style.replace(QLatin1String(kv.k), QLatin1String(kv.v));

    const QColor pc(QStringLiteral("#2E7D32"));
    const auto rgba = [&](const QString &a) {
        return QStringLiteral("rgba(%1, %2, %3, %4)")
            .arg(pc.red()).arg(pc.green()).arg(pc.blue()).arg(a);
    };
    static const struct { const char *k; QString v; } dyn[] = {
        {"@PRIMARY@", pc.name()},
        {"@PRIMARY_HOVER@", pc.darker(110).name()},
        {"@PRIMARY_PRESSED@", pc.darker(125).name()},
        {"@PRIMARY_BG@", dark ? QStringLiteral("#2d4d2d") : pc.lighter(180).name()},
        {"@PRIMARY_RGBA_12@", rgba(QStringLiteral("12"))},
        {"@PRIMARY_RGBA_20@", rgba(QStringLiteral("0.2"))},
        {"@PRIMARY_RGBA_38@", rgba(QStringLiteral("38"))},
        {"@TEXT_ON_PRIMARY@", QStringLiteral("#ffffff")},
        {"@SUCCESS@", QStringLiteral("#10B981")},
    };
    for (const auto &kv : dyn)
        style.replace(QLatin1String(kv.k), kv.v);

    static const QRegularExpression re(QStringLiteral("@([A-Z0-9_.]+)@"));
    style.replace(re, QStringLiteral("#ececec"));
    return style;
}

// 修复后的列表刷新：与 SettingsJavaPage::refreshJavaList 逻辑一致
void refreshList(QListWidget *list, const QList<QPair<QString, QString>> &installations)
{
    list->clear();

    if (installations.isEmpty()) {
        // 空状态标签每轮新建（修复：不再持有跨 clear() 的成员指针）
        auto *emptyLabel = new QLabel(
            QStringLiteral("尚未发现任何 Java 安装。\n点击上方「全盘扫描」或「手动添加」以检测 Java。"),
            list);
        emptyLabel->setObjectName(QStringLiteral("javaEmptyLabel"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);

        auto *emptyItem = new QListWidgetItem(list);
        emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        list->addItem(emptyItem);
        list->setItemWidget(emptyItem, emptyLabel);
        emptyItem->setSizeHint(emptyLabel->sizeHint().expandedTo(QSize(0, 80)));
        return;
    }

    for (const auto &inst : installations) {
        auto *item = new QListWidgetItem(list);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        auto *row = new QLabel(
            QStringLiteral("JDK %1  %2").arg(inst.second, inst.first), list);
        list->addItem(item);
        list->setItemWidget(item, row);
        item->setSizeHint(QSize(0, 64));
    }
}

// 统计画面中“尚未发现”文字是否可见
bool emptyTextVisible(QWidget *win)
{
    const auto labels = win->findChildren<QLabel *>(QStringLiteral("javaEmptyLabel"));
    for (QLabel *l : labels) {
        if (l->isVisible())
            return true;
    }
    return false;
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
    qApp->setStyleSheet(resolveTokens(raw, false));

    QWidget win;
    win.setFixedSize(760, 520);
    auto *layout = new QVBoxLayout(&win);
    layout->setContentsMargins(24, 16, 24, 16);
    auto *title = new QLabel(QStringLiteral("已安装的 Java"), &win);
    title->setObjectName(QStringLiteral("javaListTitle"));
    layout->addWidget(title);

    auto *list = new QListWidget(&win);
    list->setObjectName(QStringLiteral("javaListWidget"));
    list->setFrameShape(QFrame::NoFrame);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setFocusPolicy(Qt::NoFocus);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(list, 1);

    // 场景A：非空初始化（修复前会浮出空状态文字）
    refreshList(list, {{"C:\\jdk17\\bin\\javaw.exe", "17.0.11"},
                       {"C:\\jdk21\\bin\\javaw.exe", "21.0.4"}});
    win.show();
    app.processEvents();
    const bool a = emptyTextVisible(list->window());
    win.grab().save(QStringLiteral("D:/BlockBox/_out/java_empty_fix_a_nonempty.png"));
    std::printf("[A] non-empty init, empty-text visible: %s (expect NO)\n",
                a ? "YES <FAIL>" : "no <PASS>");

    // 场景B：清空 → 空状态显示
    refreshList(list, {});
    app.processEvents();
    const bool b = emptyTextVisible(list->window());
    win.grab().save(QStringLiteral("D:/BlockBox/_out/java_empty_fix_b_empty.png"));
    std::printf("[B] empty state, empty-text visible: %s (expect YES)\n",
                b ? "yes <PASS>" : "NO <FAIL>");

    // 场景C：空 → 加载 Java（修复前的悬空指针 + 文字残留路径）
    refreshList(list, {{"C:\\jdk17\\bin\\javaw.exe", "17.0.11"}});
    app.processEvents();
    const bool c = emptyTextVisible(list->window());
    win.grab().save(QStringLiteral("D:/BlockBox/_out/java_empty_fix_c_loaded.png"));
    std::printf("[C] after load, empty-text visible: %s (expect NO)\n",
                c ? "YES <FAIL>" : "no <PASS>");

    // 场景D：加载 → 再次全空（修复前会用悬空指针）
    refreshList(list, {});
    refreshList(list, {});
    app.processEvents();
    const bool d = emptyTextVisible(list->window());
    win.grab().save(QStringLiteral("D:/BlockBox/_out/java_empty_fix_d_reempty.png"));
    std::printf("[D] double empty refresh, empty-text visible: %s (expect YES)\n",
                d ? "yes <PASS>" : "NO <FAIL>");

    return (a || !b || c || !d) ? 1 : 0;
}
