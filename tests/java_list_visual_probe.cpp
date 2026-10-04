/**
 * @file   java_list_visual_probe.cpp
 * @brief  Java 管理页「已安装的 Java」列表视觉探针
 *
 * 复刻 SettingsJavaPage / InstanceJavaPage 的列表控件树与 objectName，
 * 加载真实 style.qss（token 解析后），渲染并截图：
 *   java_list_light.png  浅色
 *   java_list_dark.png   深色
 */
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QRegularExpression>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <cstdio>

namespace {

// 与 SettingsPage / InstanceJavaPage::eventFilter 一致的路径省略逻辑
class ElideFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::Resize) {
            auto *label = qobject_cast<QLabel *>(obj);
            if (label && label->property("fullPath").isValid()
                && label->width() > 40) {
                const QString full = label->property("fullPath").toString();
                const QString elided = QFontMetrics(label->font())
                                           .elidedText(full, Qt::ElideMiddle,
                                                       label->width() - 4);
                if (elided != label->text()) {
                    label->setText(elided);
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

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
            {"@DANGER@", "#c62828"}, {"@DANGER_BG@", "#ffebee"},
            {"@DANGER_BORDER@", "#ffcdd2"},
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
            {"@DANGER@", "#ff6b6b"}, {"@DANGER_BG@", "#5d2d2d"},
            {"@DANGER_BORDER@", "#8d4d4d"},
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

// 与 SettingsJavaPage::createJavaItemWidget 一致的列表项
QWidget *createJavaItemWidget(const QString &path, const QString &version)
{
    auto *itemFrame = new QFrame();
    itemFrame->setObjectName(QStringLiteral("javaItemFrame"));
    itemFrame->setFrameShape(QFrame::NoFrame);

    auto *itemLayout = new QHBoxLayout(itemFrame);
    itemLayout->setContentsMargins(12, 10, 12, 10);
    itemLayout->setSpacing(12);

    const bool isJdk = path.contains(QStringLiteral("Adoptium"))
                       || path.contains(QStringLiteral("jdk-"));
    const int major = [&version]() {
        static const QRegularExpression re(QStringLiteral("(?:1\\.)?([0-9]+)"));
        auto m = re.match(version);
        return m.hasMatch() ? m.captured(1).toInt() : 0;
    }();

    auto *avatarLabel = new QLabel(
        major > 0 ? QString::number(major) : QStringLiteral("J"), itemFrame);
    avatarLabel->setObjectName(QStringLiteral("javaItemAvatar"));
    avatarLabel->setProperty("kind", isJdk ? "jdk" : "jre");
    avatarLabel->setAlignment(Qt::AlignCenter);
    avatarLabel->setFixedSize(40, 40);

    auto *infoWidget = new QWidget(itemFrame);
    auto *infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(4);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);

    const QString titleStr = QStringLiteral("%1 %2")
        .arg(isJdk ? QStringLiteral("JDK") : QStringLiteral("JRE"))
        .arg(major > 0 ? QString::number(major) : version);

    auto *titleLabel = new QLabel(titleStr, infoWidget);
    titleLabel->setObjectName(QStringLiteral("javaItemTitle"));

    auto *versionLabel = new QLabel(version, infoWidget);
    versionLabel->setObjectName(QStringLiteral("javaItemVersion"));

    titleRow->addWidget(titleLabel);
    titleRow->addWidget(versionLabel);
    titleRow->addStretch();

    auto *pathLabel = new QLabel(path, infoWidget);
    pathLabel->setObjectName(QStringLiteral("javaItemPath"));
    pathLabel->setWordWrap(false);
    pathLabel->setMinimumWidth(0);
    pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    pathLabel->setToolTip(path);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    infoLayout->addLayout(titleRow);
    infoLayout->addWidget(pathLabel);

    pathLabel->setProperty("fullPath", path);
    static ElideFilter elideFilter;
    pathLabel->installEventFilter(&elideFilter);

    auto *actionsWidget = new QWidget(itemFrame);
    auto *actionsLayout = new QHBoxLayout(actionsWidget);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);

    auto *openFolderBtn = new QPushButton(QStringLiteral("打开文件夹"), actionsWidget);
    openFolderBtn->setObjectName(QStringLiteral("javaOpenFolderBtn"));
    openFolderBtn->setCursor(Qt::PointingHandCursor);

    auto *removeBtn = new QPushButton(QStringLiteral("移除"), actionsWidget);
    removeBtn->setObjectName(QStringLiteral("javaRemoveBtn"));
    removeBtn->setCursor(Qt::PointingHandCursor);

    actionsLayout->addWidget(openFolderBtn);
    actionsLayout->addWidget(removeBtn);

    itemLayout->addWidget(avatarLabel, 0, Qt::AlignVCenter);
    itemLayout->addWidget(infoWidget, 1);
    itemLayout->addWidget(actionsWidget, 0, Qt::AlignVCenter);

    return itemFrame;
}

QWidget *buildContent(QWidget *parent)
{
    auto *root = new QWidget(parent);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    auto *titleLabel = new QLabel(QStringLiteral("Java管理"), root);
    titleLabel->setObjectName(QStringLiteral("sectionTitle"));
    auto *subtitle = new QLabel(
        QStringLiteral("管理本机已安装的 Java 运行时，扫描结果会自动缓存供启动游戏时使用。"),
        root);
    subtitle->setObjectName(QStringLiteral("sectionSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(titleLabel);
    layout->addWidget(subtitle);

    // 工具栏
    auto *toolbar = new QWidget(root);
    toolbar->setObjectName(QStringLiteral("javaToolbar"));
    auto *tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(0, 0, 0, 0);
    tb->setSpacing(8);
    const char *btnNames[] = {"javaScanBtn", "javaAddBtn", "javaDetectBtn"};
    const char *btnTexts[] = {"全盘扫描", "手动添加", "快速检测"};
    for (int i = 0; i < 3; ++i) {
        auto *b = new QPushButton(QString::fromUtf8(btnTexts[i]), toolbar);
        b->setObjectName(QString::fromUtf8(btnNames[i]));
        b->setCursor(Qt::PointingHandCursor);
        tb->addWidget(b);
    }
    tb->addStretch();
    auto *dl = new QPushButton(QStringLiteral("下载Java"), toolbar);
    dl->setObjectName(QStringLiteral("downloadJavaBtn"));
    dl->setCursor(Qt::PointingHandCursor);
    tb->addWidget(dl);
    layout->addWidget(toolbar);

    auto *listTitle = new QLabel(QStringLiteral("已安装的 Java"), root);
    listTitle->setObjectName(QStringLiteral("javaListTitle"));
    layout->addWidget(listTitle);

    auto *list = new QListWidget(root);
    list->setObjectName(QStringLiteral("javaListWidget"));
    list->setFrameShape(QFrame::NoFrame);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setFocusPolicy(Qt::NoFocus);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    list->setMinimumHeight(180);

    struct Item { const char *path; const char *ver; };
    const Item items[] = {
        {"C:\\Program Files\\Eclipse Adoptium\\jdk-21.0.4.7-hotspot\\bin\\javaw.exe",
         "21.0.4"},
        {"C:\\Program Files\\Java\\jre1.8.0_301\\bin\\javaw.exe",
         "1.8.0_301"},
        {"D:\\develop\\jdk-17.0.11+9\\bin\\javaw.exe",
         "17.0.11"},
    };
    for (const auto &it : items) {
        auto *item = new QListWidgetItem(list);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        QWidget *w = createJavaItemWidget(QString::fromUtf8(it.path),
                                          QString::fromUtf8(it.ver));
        list->addItem(item);
        list->setItemWidget(item, w);
        item->setSizeHint(w->sizeHint().expandedTo(QSize(0, 72)));
    }
    layout->addWidget(list, 1);
    layout->addStretch();

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

    struct Shot { const char *name; bool dark; int w; bool hover; };
    const Shot shots[] = {
        {"java_list_light.png", false, 860, false},
        {"java_list_dark.png", true, 860, false},
        {"java_list_narrow.png", false, 460, false},
        {"java_list_hover.png", false, 860, true},
    };

    for (const Shot &s : shots) {
        qApp->setStyleSheet(resolveTokens(raw, s.dark));

        QWidget win;
        win.setFixedSize(s.w, 620);
        auto *rootLayout = new QVBoxLayout(&win);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->addWidget(buildContent(&win));

        win.show();
        app.processEvents();

        if (s.hover) {
            // 模拟第二行 hover（QSS :hover 由 HoverEnter 事件驱动）
            const auto frames = win.findChildren<QFrame *>(
                QStringLiteral("javaItemFrame"));
            if (frames.size() > 1) {
                QFrame *f = frames[1];
                f->setAttribute(Qt::WA_UnderMouse, true);
                QEvent enter(QEvent::HoverEnter);
                QApplication::sendEvent(f, &enter);
                f->update();
                app.processEvents();
            }
        }

        const QString out = QStringLiteral("D:/BlockBox/_out/") + QLatin1String(s.name);
        const bool ok = win.grab().save(out);
        std::fprintf(stderr, "[%s] %s\n", ok ? "ok" : "FAIL", qUtf8Printable(out));
        win.close();
    }
    QTimer::singleShot(100, &app, &QCoreApplication::quit);
    return app.exec();
}
