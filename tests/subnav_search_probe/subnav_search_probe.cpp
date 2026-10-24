/**
 * @file   subnav_search_probe.cpp
 * @brief  左侧子导航气泡顶部搜索框的渲染探针（改前/改后视觉比对用）
 *
 * 不链接主程序：直接读入一份「已解析 @TOKEN@」的完整 QSS，按
 * components/SubNavPanel.cpp 的真实 objectName、布局边距与控件属性重建气泡，
 * 再逐个状态（默认 / 悬停 / 聚焦 / 已输入）截图输出 PNG。
 *
 * 用法：
 *   subnav_search_probe.exe <resolved.qss> <out_prefix>
 *   -> <out_prefix>_default.png / _hover.png / _focus.png / _typing.png
 */
#include <QApplication>
#include <QFile>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

namespace {

/// 用 QPainter 画一个放大镜图标，替换工程里的 search.svg（探针不加载 qrc）
QPixmap makeSearchPixmap(const QColor &color, int size)
{
    QPixmap pm(size * 2, size * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    const qreal d = size * 0.62;
    p.drawEllipse(QRectF(2.0, 2.0, d, d));
    p.drawLine(QPointF(2.0 + d * 0.78, 2.0 + d * 0.78),
               QPointF(2.0 + d * 1.22, 2.0 + d * 1.22));
    p.end();
    return pm;
}

/// 用 QPainter 画一个左向 chevron（收起按钮图标）
QPixmap makeChevronPixmap(const QColor &color, int size)
{
    QPixmap pm(size * 2, size * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    QPainterPath path;
    path.moveTo(size * 0.62, 1.6);
    path.lineTo(size * 0.36, size * 0.5);
    path.lineTo(size * 0.62, size - 1.6);
    p.drawPath(path);
    p.end();
    return pm;
}

struct Bubble {
    QWidget *panel = nullptr;
    QWidget *searchContainer = nullptr;
    QLabel *searchIcon = nullptr;
    QLineEdit *searchEdit = nullptr;
};

/// QSS 里 `SubNavPanel { ... }` 是按类名匹配的，探针必须用同名类才能吃到气泡底色/圆角
class SubNavPanel : public QWidget
{
public:
    using QWidget::QWidget;
};

/**
 * @brief 按 SubNavPanel.cpp 的构造逻辑重建气泡（仅视觉相关部分）
 * @param iconColor 搜索图标颜色（代码里是固定 #666666）
 * @param legacy    true 时复原改造前的结构与边距，用于「改前」对照
 */
Bubble buildBubble(const QColor &iconColor, const QColor &chevronColor, bool legacy)
{
    Bubble b;

    b.panel = new SubNavPanel;
    b.panel->setObjectName(QStringLiteral("SubNavPanel"));
    b.panel->setAttribute(Qt::WA_StyledBackground, true);
    b.panel->setFixedSize(240, 400);

    auto *mainLayout = new QVBoxLayout(b.panel);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── 面板头部 ──
    auto *header = new QWidget(b.panel);
    header->setObjectName(QStringLiteral("subNavHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 12, 12, 12);
    headerLayout->setSpacing(4);

    auto *title = new QLabel(QStringLiteral("多人游戏"), header);
    title->setObjectName(QStringLiteral("subNavHeaderTitle"));

    auto *collapseBtn = new QPushButton(header);
    collapseBtn->setObjectName(QStringLiteral("subNavCollapseBtn"));
    collapseBtn->setIcon(QIcon(makeChevronPixmap(chevronColor, 16)));
    collapseBtn->setIconSize(QSize(16, 16));
    collapseBtn->setFixedSize(24, 24);

    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(collapseBtn);
    mainLayout->addWidget(header);

    // ── 顶部搜索框（SubNavPanel.cpp 构造逻辑；legacy=true 复原改造前的结构与边距）──
    auto *searchRow = new QWidget(b.panel);
    searchRow->setObjectName(QStringLiteral("subNavSearchRow"));
    auto *rowLayout = new QHBoxLayout(searchRow);
    if (legacy) {
        rowLayout->setContentsMargins(0, 0, 0, 0);   // 改造前：容器直接占满气泡宽度
    } else {
        rowLayout->setContentsMargins(12, 0, 12, 12);
    }
    rowLayout->setSpacing(0);

    auto *searchContainer = new QWidget(legacy ? b.panel : searchRow);
    searchContainer->setObjectName(QStringLiteral("subNavSearchContainer"));
    searchContainer->setAttribute(Qt::WA_Hover, true);
    auto *searchLayout = new QHBoxLayout(searchContainer);
    if (legacy) {
        searchLayout->setContentsMargins(12, 0, 12, 8);  // 改造前：底部多出 8px 空条
        searchLayout->setSpacing(0);
    } else {
        searchLayout->setContentsMargins(10, 0, 10, 0);
        searchLayout->setSpacing(6);
    }

    auto *searchIcon = new QLabel(searchContainer);
    searchIcon->setObjectName(QStringLiteral("subNavSearchIcon"));
    if (legacy) {
        searchIcon->setFixedWidth(30);
    } else {
        searchIcon->setFixedSize(16, 16);
    }
    searchIcon->setAlignment(Qt::AlignCenter);
    searchIcon->setPixmap(makeSearchPixmap(iconColor, legacy ? 15 : 16));

    auto *searchEdit = new QLineEdit(searchContainer);
    searchEdit->setObjectName(QStringLiteral("subNavSearchEdit"));
    searchEdit->setPlaceholderText(QStringLiteral("搜索当前页面内容"));
    searchEdit->setClearButtonEnabled(true);

    rowLayout->addWidget(searchContainer);
    searchLayout->addWidget(searchIcon);
    searchLayout->addWidget(searchEdit, 1);
    mainLayout->addWidget(legacy ? searchContainer : searchRow);

    // ── 列表（上下文参考）──
    auto *scroll = new QScrollArea(b.panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(8, 12, 12, 16);
    contentLayout->setSpacing(2);
    for (const QString &txt : {QStringLiteral("服务器列表"), QStringLiteral("添加服务器"),
                               QStringLiteral("收藏夹"), QStringLiteral("局域网发现")}) {
        auto *btn = new QPushButton(txt, content);
        btn->setObjectName(QStringLiteral("subNavButton"));
        btn->setFixedHeight(40);
        btn->setCheckable(true);
        contentLayout->addWidget(btn);
    }
    contentLayout->addStretch();
    scroll->setWidget(content);
    mainLayout->addWidget(scroll);

    b.searchContainer = searchContainer;
    b.searchIcon = searchIcon;
    b.searchEdit = searchEdit;
    return b;
}

QWidget *wrapOnPage(QWidget *bubble, const QColor &pageBg)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("probePageHost"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("QWidget#probePageHost { background-color: %1; }")
                            .arg(pageBg.name()));
    page->resize(300, 440);
    bubble->setParent(page);
    bubble->move(12, 20);
    return page;
}

} // namespace

namespace {

/// 采样胶囊的描边色与底色，用于给「改前 / 改后」做可复现的像素断言
struct Sample {
    QColor border;   ///< 胶囊顶边正中像素
    QColor fill;     ///< 胶囊内部左侧空白处像素
    QRect pill;      ///< 胶囊在整页坐标系下的矩形
};

Sample samplePill(QWidget *page, QWidget *container)
{
    Sample s;
    const QPixmap pm = page->grab();
    const QImage img = pm.toImage();
    s.pill = QRect(container->mapTo(page, QPoint(0, 0)), container->size());
    const int cx = s.pill.center().x();
    const int cy = s.pill.center().y();
    s.border = img.pixelColor(cx, s.pill.top());
    s.fill = img.pixelColor(s.pill.left() + 6, cy);
    return s;
}

void report(const QString &tag, const Sample &s)
{
    std::fprintf(stderr, "  %-9s pill=%dx%d  border=%s  fill=%s\n",
                 qUtf8Printable(tag), s.pill.width(), s.pill.height(),
                 qUtf8Printable(s.border.name()), qUtf8Printable(s.fill.name()));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BlockBoxProbe"));
    QCoreApplication::setApplicationName(QStringLiteral("subnav_search_probe"));

    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <resolved.qss> <out_prefix> [page_bg_hex]\n", argv[0]);
        return 2;
    }
    const QString qssPath = QString::fromLocal8Bit(argv[1]);
    const QString prefix = QString::fromLocal8Bit(argv[2]);
    const QColor pageBg(argc > 3 ? QString::fromLocal8Bit(argv[3]) : QStringLiteral("#f5f5f5"));
    const bool legacy = (argc > 4 && qstrcmp(argv[4], "legacy") == 0);

    QFile f(qssPath);
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open %s\n", qPrintable(qssPath));
        return 2;
    }
    const QString style = QString::fromUtf8(f.readAll());
    f.close();
    qApp->setStyleSheet(style);

    // 与工程实际的图标着色保持一致的近似值（分子色由代码按主题算，这里取浅色主题结果）
    const QColor iconColor(QStringLiteral("#a3a3a3"));
    const QColor chevronColor(QStringLiteral("#888888"));
    const QColor primary(QStringLiteral("#2E7D32"));

    const double scale = 2.0;

    // ── 状态 1：默认（未聚焦、无内容）──
    Sample def;
    {
        Bubble b = buildBubble(iconColor, chevronColor, legacy);
        QWidget *page = wrapOnPage(b.panel, pageBg);
        page->show();
        QApplication::processEvents();
        def = samplePill(page, b.searchContainer);
        QPixmap pm = page->grab();
        pm.setDevicePixelRatio(scale);
        pm.save(prefix + QStringLiteral("_default.png"));
        std::fprintf(stderr, "[ok ] %s_default.png  容器 %dx%d 输入框 %dx%d 图标 %dx%d\n",
                     qPrintable(prefix), b.searchContainer->width(), b.searchContainer->height(),
                     b.searchEdit->width(), b.searchEdit->height(),
                     b.searchIcon->width(), b.searchIcon->height());
        report(QStringLiteral("default"), def);
        page->hide();
    }

    // ── 状态 2：悬停（Enter 派给胶囊本身；真实运行时光标落在输入框上，
    //            Qt 会把 Enter 沿父链一并补发给胶囊，:hover 同样成立）──
    {
        Bubble b = buildBubble(iconColor, chevronColor, legacy);
        QWidget *page = wrapOnPage(b.panel, pageBg);
        page->show();
        QApplication::processEvents();
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(b.searchContainer, &enter);
        QCoreApplication::processEvents();
        const Sample s = samplePill(page, b.searchContainer);
        QPixmap pm = page->grab();
        pm.setDevicePixelRatio(scale);
        pm.save(prefix + QStringLiteral("_hover.png"));
        std::fprintf(stderr, "[ok ] %s_hover.png\n", qPrintable(prefix));
        report(QStringLiteral("hover"), s);
        std::fprintf(stderr, "        悬停描边 %s 与常态 %s %s\n",
                     qUtf8Printable(s.border.name()), qUtf8Printable(def.border.name()),
                     s.border != def.border ? "不同（hover 生效）" : "相同（hover 未生效）");
        page->hide();
    }

    // ── 状态 3：聚焦（SubNavPanel 的 eventFilter 会置 searchFocus 属性）──
    {
        Bubble b = buildBubble(iconColor, chevronColor, legacy);
        QWidget *page = wrapOnPage(b.panel, pageBg);
        page->show();
        QApplication::processEvents();
        b.searchContainer->setProperty("searchFocus", true);
        b.searchContainer->style()->unpolish(b.searchContainer);
        b.searchContainer->style()->polish(b.searchContainer);
        b.searchIcon->setPixmap(makeSearchPixmap(primary, 16));
        b.searchEdit->setFocus();
        QCoreApplication::processEvents();
        const Sample s = samplePill(page, b.searchContainer);
        QPixmap pm = page->grab();
        pm.setDevicePixelRatio(scale);
        pm.save(prefix + QStringLiteral("_focus.png"));
        std::fprintf(stderr, "[ok ] %s_focus.png\n", qPrintable(prefix));
        report(QStringLiteral("focus"), s);
        std::fprintf(stderr, "        聚焦描边 %s（%s）\n", qUtf8Printable(s.border.name()),
                     s.border != def.border ? "与常态不同" : "与常态相同");
        page->hide();
    }

    // ── 状态 4：已输入（图标转主色 + 清除按钮出现）──
    {
        Bubble b = buildBubble(iconColor, chevronColor, legacy);
        QWidget *page = wrapOnPage(b.panel, pageBg);
        page->show();
        QCoreApplication::processEvents();
        b.searchContainer->setProperty("searchFocus", true);
        b.searchContainer->style()->unpolish(b.searchContainer);
        b.searchContainer->style()->polish(b.searchContainer);
        b.searchEdit->setText(QStringLiteral("Sodium"));
        b.searchIcon->setPixmap(makeSearchPixmap(primary, 16));
        b.searchEdit->setFocus();
        QCoreApplication::processEvents();
        const Sample s = samplePill(page, b.searchContainer);
        QPixmap pm = page->grab();
        pm.setDevicePixelRatio(scale);
        pm.save(prefix + QStringLiteral("_typing.png"));
        std::fprintf(stderr, "[ok ] %s_typing.png\n", qPrintable(prefix));
        report(QStringLiteral("typing"), s);
        page->hide();
    }

    return 0;
}
