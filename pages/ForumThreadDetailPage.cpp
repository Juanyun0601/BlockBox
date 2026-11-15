/**
 * @file   ForumThreadDetailPage.cpp
 * @brief  论坛帖子详情页实现（版面与网页版 thread.html 对齐）
 * @author BlockBox Team
 * @date   2026-11-01
 */

#include "ForumThreadDetailPage.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextCursor>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "components/AppInputDialog.h"
#include "components/AppMessageBox.h"
#include "components/ForumLinkRowsEditor.h"
#include "components/NotificationManager.h"
#include "components/PublishKit.h"
#include "components/QuizDialog.h"
#include "components/ShareToFriendDialog.h"
#include "components/ThreadTipDialog.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/IconHelper.h"
#include "utils/StyleKit.h"
#include "utils/plugin/PluginManager.h"
#include "utils/plugin/PluginSafetyGuard.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

/** 右栏宽度，和网页版 .thread-layout 的第二列一致 */
constexpr int kRailWidth = 300;

/** 插件帖左栏（下载 + 插件信息）宽度，与资源详情页的侧栏观感对齐 */
constexpr int kPluginRailWidth = 320;

/** 放不下两栏就上下叠，网页版断点 1080px */
constexpr int kStackBelow = 1080;

/** 楼中楼先显示几条，其余的折叠起来 */
constexpr int kVisibleChildren = 6;

QString authorDisplay(const ForumUser &author)
{
    if (!author.displayName.isEmpty())
        return author.displayName;
    if (!author.username.isEmpty())
        return author.username;
    return QObject::tr("未知用户");
}

/** 楼层正文摘要：去掉常见 Markdown 标记后取前 80 字，给举报页展示 */
QString postExcerptOf(const QString &content)
{
    QString text = content;
    text.remove(QRegularExpression(QStringLiteral("!\\[[^\\]]*\\]\\([^)]*\\)")));   // 图片
    text.replace(QRegularExpression(QStringLiteral("\\[([^\\]]*)\\]\\([^)]*\\)")),
                 QStringLiteral("\\1"));                                            // 链接留文字
    text.remove(QRegularExpression(QStringLiteral("`{1,3}")));                      // 行内代码 / 代码块
    text.remove(QRegularExpression(QStringLiteral("\\*\\*|__|~~|\\*|_")));          // 强调
    text.remove(QRegularExpression(QStringLiteral("(?m)^#{1,6}\\s*")));             // 标题
    text.remove(QRegularExpression(QStringLiteral("(?m)^>\\s?")));                  // 引用
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    text = text.trimmed();
    if (text.size() > 80)
        text = text.left(80) + QStringLiteral("…");
    return text;
}

QString joinedText(const ForumUser &author)
{
    if (author.createdAt <= 0)
        return QString();
    return QObject::tr("注册于 %1")
        .arg(QDateTime::fromSecsSinceEpoch(author.createdAt).toString(QStringLiteral("yyyy-MM-dd")));
}

QString absoluteTime(qint64 seconds)
{
    if (seconds <= 0)
        return QString();
    return QDateTime::fromSecsSinceEpoch(seconds)
        .toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

/** 圆角方形插图：等比铺满后居中裁切，再按圆角路径裁掉四角（插件大图标用） */
QPixmap roundedPixmap(const QImage &image, int size, qreal radius)
{
    if (image.isNull())
        return QPixmap();

    const QPixmap scaled = QPixmap::fromImage(
        image.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    const int cropX = qMax(0, (scaled.width() - size) / 2);
    const int cropY = qMax(0, (scaled.height() - size) / 2);

    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
    painter.setClipPath(clip);
    painter.drawPixmap(0, 0, scaled, cropX, cropY, size, size);
    painter.end();
    return canvas;
}

/**
 * @brief 主题头卡片：普通帖与插件帖共用，顶部画一条 3px 品牌渐变
 *
 * 网页版 thread.html 的 .thread-hero::after 就是这一条：贴着圆角画，
 * 不越过 1px 描边，标题区因此多一层主色，不会显得是一张纯白卡。
 */
class ForumHeroFrame : public QFrame
{
public:
    explicit ForumHeroFrame(QWidget *parent = nullptr) : QFrame(parent) {}

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // 裁进卡片内圈（描边内侧），顶角跟着圆角走
        QPainterPath clip;
        clip.addRoundedRect(QRectF(1, 1, width() - 2, height() - 2), 15, 15);
        painter.setClipPath(clip);

        QLinearGradient gradient(0, 0, width(), 0);
        gradient.setColorAt(0.0, QColor(StyleKit::resolve(QStringLiteral("@PRIMARY@"))));
        gradient.setColorAt(1.0, QColor(StyleKit::resolve(QStringLiteral("@PRIMARY_GRAD1@"))));
        painter.fillRect(QRectF(1, 1, width() - 2, 3), gradient);
    }
};

/** 胶囊操作按钮（回复 / 引用 / 点赞 / 编辑…） */
QPushButton *makeAct(QWidget *parent, const QString &text, bool danger = false)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("forumAct"));
    button->setProperty("danger", danger);
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    button->setFixedHeight(28);
    return button;
}

} // namespace

ForumThreadDetailPage::ForumThreadDetailPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    connect(ForumAccountService::instance(), &ForumAccountService::userChanged, this,
            [this]() {
                updateReplyBoxState();
                renderHero();
            });
    connect(ForumAccountService::instance(), &ForumAccountService::sessionExpired, this,
            [this]() {
                updateReplyBoxState();
                renderHero();
            });
}

// ────────────────────────────────────────────────────────────── 骨架

ForumThreadDetailPage::ResourceKind ForumThreadDetailPage::resourceKind() const
{
    if (m_info.plugin.isValid())
        return ResourceKind::Plugin;
    if (m_info.schematic.isValid())
        return ResourceKind::Projection;

    // 老帖子可能只落在分享板块里、帖子上却没挂资源记录（服务端早期只存了正文），
    // 按板块认：这两个板块里的帖子天生就是资源详情页。
    if (m_info.boardSlug == QLatin1String("plugin-share"))
        return ResourceKind::Plugin;
    if (m_info.boardSlug == QLatin1String("schematic-share"))
        return ResourceKind::Projection;
    return ResourceKind::None;
}

void ForumThreadDetailPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    // 滚动区真正拿到宽度是在首次布局之后，靠 viewport 的 resize 才能量到准数
    m_scroll->viewport()->installEventFilter(this);

    m_content = new QWidget(m_scroll);
    m_content->setAttribute(Qt::WA_StyledBackground, false);
    auto *page = new QVBoxLayout(m_content);
    page->setContentsMargins(26, 16, 26, 28);
    page->setSpacing(14);
    m_pageLayout = page;

    // ── 状态行：只在加载 / 出错时出现，平时不占版面 ──
    m_statusLabel = new QLabel(m_content);
    m_statusLabel->setObjectName(QStringLiteral("forumStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    page->addWidget(m_statusLabel);

    // 页面顶部的「论坛首页 › 板块 › 帖子」面包屑不再画：顶栏那个搜索框里
    // 已经写着「方块盒子>社区 > 帖子」，同一个地址没必要出现两次，正文直接顶上来。

    // ── 两栏主体 ──
    m_bodyLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(22);

    m_mainColumn = new QWidget(m_content);
    m_mainColumn->setAttribute(Qt::WA_StyledBackground, false);
    m_mainLayout = new QVBoxLayout(m_mainColumn);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(14);

    // 主题头
    m_heroCard = new ForumHeroFrame(m_mainColumn);
    m_heroCard->setObjectName(QStringLiteral("forumHero"));
    m_heroBody = new QVBoxLayout(m_heroCard);
    m_heroBody->setContentsMargins(24, 24, 24, 22);
    m_heroBody->setSpacing(10);
    m_heroBody->addWidget(ForumKit::makeMuted(m_heroCard, tr("正在加载主题…")));
    m_mainLayout->addWidget(m_heroCard);

    // 资源帖的「详细信息」：作者发帖时写的介绍正文，摆评论前面；教程帖这里是「教程正文」
    // （目录锚点靠它跳）；普通帖不需要，默认藏起来
    m_bodyTitle = new QLabel(tr("详细信息"), m_mainColumn);
    m_bodyTitle->setObjectName(QStringLiteral("forumSectionTitle"));
    m_bodyTitle->hide();
    m_mainLayout->addWidget(m_bodyTitle);

    m_bodyCard = new QFrame(m_mainColumn);
    m_bodyCard->setObjectName(QStringLiteral("forumBodyCard"));
    auto *bodyCardLayout = new QVBoxLayout(m_bodyCard);
    bodyCardLayout->setContentsMargins(20, 16, 20, 18);
    bodyCardLayout->setSpacing(0);
    m_bodyBrowser = new ForumPostBrowser(m_bodyCard);
    bodyCardLayout->addWidget(m_bodyBrowser);
    // 和楼层正文同一套交互：图片点开看大图，内链跳转、外链开浏览器
    connect(m_bodyBrowser, &ForumPostBrowser::imageClicked, this, [](const QString &path) {
        ForumPostBrowser::showImagePreview(path);
    });
    connect(m_bodyBrowser, &ForumPostBrowser::linkClicked, this,
            &ForumThreadDetailPage::handleContentLink);
    m_bodyCard->hide();
    m_mainLayout->addWidget(m_bodyCard);

    // 资源帖右栏的小标题（「插件介绍与讨论」/「投影介绍与讨论」）：普通帖不需要，默认藏起来
    m_discussionHead = new QWidget(m_mainColumn);
    m_discussionHead->setAttribute(Qt::WA_StyledBackground, false);
    auto *discussLayout = new QHBoxLayout(m_discussionHead);
    discussLayout->setContentsMargins(2, 2, 2, 0);
    discussLayout->setSpacing(8);
    m_discussionTitle = new QLabel(tr("插件介绍与讨论"), m_discussionHead);
    m_discussionTitle->setObjectName(QStringLiteral("forumSectionTitle"));
    discussLayout->addWidget(m_discussionTitle);
    m_discussionCount = new QLabel(m_discussionHead);
    m_discussionCount->setObjectName(QStringLiteral("forumCountPill"));
    discussLayout->addWidget(m_discussionCount);
    discussLayout->addStretch(1);
    m_discussionHead->hide();
    m_mainLayout->addWidget(m_discussionHead);

    // 楼层列表
    m_floorsLayout = new QVBoxLayout();
    m_floorsLayout->setContentsMargins(0, 0, 0, 0);
    m_floorsLayout->setSpacing(14);
    m_mainLayout->addLayout(m_floorsLayout);

    // 分页
    m_pager = new ForumKit::ForumPager(m_mainColumn);
    connect(m_pager, &ForumKit::ForumPager::pageRequested, this, [this](int page) {
        // 资源帖的页签是评论分页，普通帖是楼层分页
        if (isResourceThread())
            loadComments(page);
        else
            loadPage(page);
    });
    m_mainLayout->addWidget(m_pager);

    // 回复框（普通帖）
    buildComposerCard();
    m_mainLayout->addWidget(m_composerCard);

    // 评论输入卡（资源帖）：和回复框互斥出现
    buildCommentComposerCard();
    m_mainLayout->addWidget(m_commentComposerCard);
    m_mainLayout->addStretch(1);

    // 右栏
    m_railHost = new QWidget(m_content);
    m_railHost->setAttribute(Qt::WA_StyledBackground, false);
    m_railLayout = new QVBoxLayout(m_railHost);
    m_railLayout->setContentsMargins(0, 0, 0, 0);
    m_railLayout->setSpacing(14);
    buildRail();

    m_bodyLayout->addWidget(m_mainColumn, 1);
    m_bodyLayout->addWidget(m_railHost, 0);
    page->addLayout(m_bodyLayout);
    page->addStretch(1);

    m_scroll->setWidget(m_content);
    outer->addWidget(m_scroll);

    applyPageWidth();
    renderComposer();
    applyLayoutMode();
}

void ForumThreadDetailPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyPageWidth();
    applyLayoutMode();
}

void ForumThreadDetailPage::applyPageWidth()
{
    if (!m_scroll || !m_pageLayout)
        return;

    // 宽屏下把两栏收在一条阅读宽度里居中：帖子正文一行拉过 900px 就很难读了
    constexpr int kMaxPageWidth = 1240;
    constexpr int kMinSideMargin = 26;

    const int available = m_scroll->viewport()->width();
    if (available <= 0)
        return;

    const int side = available > kMaxPageWidth + kMinSideMargin * 2
                         ? (available - kMaxPageWidth) / 2
                         : kMinSideMargin;
    const QMargins margins = m_pageLayout->contentsMargins();
    if (margins.left() == side && margins.right() == side)
        return;
    m_pageLayout->setContentsMargins(side, 16, side, 28);
}

void ForumThreadDetailPage::applyLayoutMode()
{
    if (!m_bodyLayout)
        return;

    // 插件帖 / 投影帖按资源详情页排：主题头通栏，左栏下载，右栏看资源详情与评论
    const bool resourceMode = isResourceThread();
    // 窄窗口把侧栏折到主栏下面，避免正文被挤成一条
    const bool stacked = width() > 0 && width() < kStackBelow;

    // 讨论区小标题按帖子类型换文案；放这里是因为它随帖子内容变，而版面可能没变不用重排
    if (m_discussionHead)
    {
        m_discussionHead->setVisible(resourceMode);
        if (m_discussionTitle)
            m_discussionTitle->setText(tr("评论"));
        updateCommentHeader();
    }

    // 版面没变就不动控件：resize 每一帧都重排会让滚动页抖一下
    const int mode = (resourceMode ? 1 : 0) | (stacked ? 2 : 0);
    if (mode == m_layoutMode)
        return;
    m_layoutMode = mode;

    placeHero(resourceMode);

    m_bodyLayout->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_bodyLayout->setSpacing(stacked ? 16 : 22);

    // 资源帖把下载栏放左边（第一列），普通帖把正文放左边
    // insertWidget 会把控件从原布局摘下来，重复调用即原地调整顺序
    if (resourceMode)
    {
        m_bodyLayout->insertWidget(0, m_railHost);
        m_bodyLayout->insertWidget(1, m_mainColumn);
        m_bodyLayout->setStretch(0, 0);
        m_bodyLayout->setStretch(1, 1);
    }
    else
    {
        m_bodyLayout->insertWidget(0, m_mainColumn);
        m_bodyLayout->insertWidget(1, m_railHost);
        m_bodyLayout->setStretch(0, 1);
        m_bodyLayout->setStretch(1, 0);
    }

    if (stacked)
    {
        m_railHost->setMinimumWidth(0);
        m_railHost->setMaximumWidth(QWIDGETSIZE_MAX);
        m_railHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    else
    {
        m_railHost->setFixedWidth(resourceMode ? kPluginRailWidth : kRailWidth);
        m_railHost->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }
}

void ForumThreadDetailPage::placeHero(bool atPageLevel)
{
    if (!m_heroCard || !m_mainLayout || !m_pageLayout || !m_bodyLayout)
        return;

    // 普通帖：主题头就在主栏最上面，已经在位就别再挪一遍
    if (!atPageLevel)
    {
        if (m_mainLayout->indexOf(m_heroCard) == 0)
            return;
        m_pageLayout->removeWidget(m_heroCard);
        m_mainLayout->insertWidget(0, m_heroCard);
        return;
    }

    // 插件帖：主题头提到整页顶部通栏，塞在两栏主体之前
    const auto bodyIndex = [this]() {
        for (int i = 0; i < m_pageLayout->count(); ++i)
        {
            if (m_pageLayout->itemAt(i)->layout() == m_bodyLayout)
                return i;
        }
        return m_pageLayout->count();
    };

    const int current = bodyIndex();
    if (current > 0 && m_pageLayout->itemAt(current - 1)->widget() == m_heroCard)
        return;   // 已经在通栏位置

    m_pageLayout->removeWidget(m_heroCard);
    // 摘掉之后，两栏主体的位置可能往前挪一位，重新算
    m_pageLayout->insertWidget(bodyIndex(), m_heroCard);
}

// ────────────────────────────────────────────────────────────── 主题头

void ForumThreadDetailPage::renderHero()
{
    if (!m_heroBody)
        return;

    ForumKit::clearLayout(m_heroBody);

    if (!m_info.isValid())
    {
        applyHeroSkin(false);
        m_heroBody->addWidget(ForumKit::makeMuted(m_heroCard, tr("选择一个帖子就能在这里看全文")));
        return;
    }

    // 插件帖 / 投影帖换成资源详情页的头部：大图标 + 名称 + 文件信息 + 简介
    const bool resourceMode = isResourceThread();
    applyHeroSkin(resourceMode);

    appendHeroFlags();
    appendHeroTitleRow(resourceMode);
    appendHeroMeta();
    appendHeroTags();
    appendHeroAdmin();
}

void ForumThreadDetailPage::applyHeroSkin(bool resourceMode)
{
    // 普通帖沿用论坛卡片；资源帖换成资源详情页的通栏头部
    m_heroBody->setContentsMargins(resourceMode ? 22 : 24, resourceMode ? 22 : 22,
                                   resourceMode ? 22 : 24, resourceMode ? 20 : 20);

    const QString skin = resourceMode ? QStringLiteral("forumPluginHero")
                                    : QStringLiteral("forumHero");
    if (m_heroCard->objectName() == skin)
        return;

    m_heroCard->setObjectName(skin);
    // 换了 objectName 要重新 polish，QSS 才会按新样式画背景和圆角
    m_heroCard->style()->unpolish(m_heroCard);
    m_heroCard->style()->polish(m_heroCard);
    m_heroCard->update();
}

void ForumThreadDetailPage::appendHeroFlags()
{
    // ── 置顶 / 锁定徽章 ──
    if (!m_info.pinned && !m_info.locked)
        return;

    auto *flagsRow = new QHBoxLayout();
    flagsRow->setSpacing(6);
    if (m_info.pinned)
        flagsRow->addWidget(ForumKit::makeBadge(m_heroCard, tr("置顶")));
    if (m_info.locked)
        flagsRow->addWidget(ForumKit::makeBadge(m_heroCard, tr("已锁定"), true));
    flagsRow->addStretch(1);
    m_heroBody->addLayout(flagsRow);
}

void ForumThreadDetailPage::appendHeroTitleRow(bool resourceMode)
{
    // 分享 / 收藏两个按钮两种版面都要，抽出来避免重复
    const auto makeShare = [this]() {
        auto *button = new QPushButton(tr("分享"), m_heroCard);
        button->setObjectName(QStringLiteral("forumShareButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(30);
        button->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/share.svg"),
            QColor(StyleKit::resolve(QStringLiteral("@TEXT_SECONDARY@"))), 13));
        button->setIconSize(QSize(13, 13));
        connect(button, &QPushButton::clicked, this, &ForumThreadDetailPage::shareThread);
        return button;
    };
    const auto makeBookmark = [this]() {
        // 文案统一用「收藏」，收藏与否靠图标变实心 + 主色描边表达
        auto *button = new QPushButton(tr("收藏"), m_heroCard);
        button->setObjectName(QStringLiteral("forumBookmarkButton"));
        button->setProperty("active", m_info.bookmarked);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(30);
        button->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/star.svg"),
            QColor(StyleKit::resolve(m_info.bookmarked ? QStringLiteral("@PRIMARY@")
                                                       : QStringLiteral("@TEXT_SECONDARY@"))),
            13));
        button->setIconSize(QSize(13, 13));
        connect(button, &QPushButton::clicked, this, &ForumThreadDetailPage::toggleBookmark);
        m_bookmarkButton = button;
        return button;
    };
    const auto makeTip = [this]() {
        auto *button = new QPushButton(tr("打赏"), m_heroCard);
        button->setObjectName(QStringLiteral("forumTipButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(30);
        button->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/tip.svg"),
            QColor(StyleKit::resolve(QStringLiteral("@TEXT_SECONDARY@"))), 13));
        button->setIconSize(QSize(13, 13));
        button->setToolTip(tr("给这个帖子打赏，提升它的推流"));
        connect(button, &QPushButton::clicked, this, &ForumThreadDetailPage::tipThread);
        return button;
    };

    if (resourceMode)
    {
        if (resourceKind() == ResourceKind::Projection)
        {
            // ── 投影帖：渐变图标 + 名称 / 文件名 + 大小·下载次数 ──
            auto *headerRow = new QHBoxLayout();
            headerRow->setSpacing(16);

            auto *icon = new QLabel(m_heroCard);
            icon->setObjectName(QStringLiteral("forumPluginHeroIcon"));
            icon->setFixedSize(76, 76);
            icon->setAlignment(Qt::AlignCenter);
            icon->setPixmap(PublishKit::makeGradientIconPixmap(
                QColor(QStringLiteral("#3B82F6")), QColor(QStringLiteral("#1D4ED8")),
                QStringLiteral(":/Images/Icons/list.svg"), 76));
            headerRow->addWidget(icon, 0, Qt::AlignTop);

            auto *textColumn = new QVBoxLayout();
            textColumn->setContentsMargins(0, 0, 0, 0);
            textColumn->setSpacing(5);

            auto *titleRow = new QHBoxLayout();
            titleRow->setSpacing(8);
            auto *title = new QLabel(m_info.title, m_heroCard);
            title->setObjectName(QStringLiteral("forumPluginHeroTitle"));
            title->setWordWrap(true);
            title->setTextInteractionFlags(Qt::TextSelectableByMouse);
            titleRow->addWidget(title, 1);
            titleRow->addWidget(makeShare(), 0, Qt::AlignTop);
            titleRow->addWidget(makeBookmark(), 0, Qt::AlignTop);
            titleRow->addWidget(makeTip(), 0, Qt::AlignTop);
            textColumn->addLayout(titleRow);

            const QString displayName = m_info.schematic.name.isEmpty()
                                            ? m_info.schematic.fileName
                                            : m_info.schematic.name;
            QStringList tagBits;
            if (!displayName.isEmpty() && displayName != m_info.title)
                tagBits << displayName;
            if (!m_info.schematic.fileName.isEmpty() && m_info.schematic.fileName != displayName)
                tagBits << m_info.schematic.fileName;
            if (!tagBits.isEmpty())
            {
                auto *sub = new QLabel(tagBits.join(QStringLiteral("  ·  ")), m_heroCard);
                sub->setObjectName(QStringLiteral("forumPluginHeroSub"));
                sub->setWordWrap(true);
                textColumn->addWidget(sub);
            }

            // 老帖子可能没挂上投影记录（只有正文），这时不摆「0 B · 0 次下载」的空信息
            if (m_info.schematic.isValid())
            {
                const QString metaBits = QStringList{
                    ForumKit::humanFileSize(m_info.schematic.fileSize),
                    tr("%1 次下载").arg(m_info.schematic.downloads),
                }.join(QStringLiteral(" · "));
                auto *schematicMeta = new QLabel(metaBits, m_heroCard);
                schematicMeta->setObjectName(QStringLiteral("forumPluginHeroMeta"));
                schematicMeta->setWordWrap(true);
                textColumn->addWidget(schematicMeta);
            }

            headerRow->addLayout(textColumn, 1);
            m_heroBody->addLayout(headerRow);
            return;
        }

        // ── 插件帖：大图标 + 名称 / 版本 + 作者·大小·下载次数 + 简介 ──
        auto *headerRow = new QHBoxLayout();
        headerRow->setSpacing(16);

        auto *icon = new QLabel(m_heroCard);
        icon->setObjectName(QStringLiteral("forumPluginHeroIcon"));
        icon->setFixedSize(76, 76);
        icon->setAlignment(Qt::AlignCenter);
        if (!m_info.plugin.icon.isEmpty())
        {
            ForumImageStore::instance()->requestImage(
                ForumAccountService::instance()->absoluteUrl(m_info.plugin.icon),
                [icon](const QImage &image) {
                    const QPixmap rounded = roundedPixmap(image, 76, 18);
                    if (!rounded.isNull())
                        icon->setPixmap(rounded);
                });
        }
        else
        {
            icon->setText(QStringLiteral("🧩"));
        }
        headerRow->addWidget(icon, 0, Qt::AlignTop);

        auto *textColumn = new QVBoxLayout();
        textColumn->setContentsMargins(0, 0, 0, 0);
        textColumn->setSpacing(5);

        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(8);
        auto *title = new QLabel(m_info.title, m_heroCard);
        title->setObjectName(QStringLiteral("forumPluginHeroTitle"));
        title->setWordWrap(true);
        title->setTextInteractionFlags(Qt::TextSelectableByMouse);
        titleRow->addWidget(title, 1);
        titleRow->addWidget(makeShare(), 0, Qt::AlignTop);
        titleRow->addWidget(makeBookmark(), 0, Qt::AlignTop);
        titleRow->addWidget(makeTip(), 0, Qt::AlignTop);
        textColumn->addLayout(titleRow);

        // 插件标识行：插件名（和帖子标题不一样时才显示）+ 版本 + 插件 ID
        QStringList tagBits;
        if (!m_info.plugin.name.isEmpty() && m_info.plugin.name != m_info.title)
            tagBits << m_info.plugin.name;
        if (!m_info.plugin.version.isEmpty())
            tagBits << tr("v%1").arg(m_info.plugin.version);
        if (!m_info.plugin.pluginId.isEmpty())
            tagBits << m_info.plugin.pluginId;
        if (!tagBits.isEmpty())
        {
            auto *sub = new QLabel(tagBits.join(QStringLiteral("  ·  ")), m_heroCard);
            sub->setObjectName(QStringLiteral("forumPluginHeroSub"));
            sub->setWordWrap(true);
            textColumn->addWidget(sub);
        }

        const QString metaBits = QStringList{
            m_info.plugin.author.isEmpty() ? tr("作者未知")
                                           : tr("作者 %1").arg(m_info.plugin.author),
            ForumKit::humanFileSize(m_info.plugin.fileSize),
            tr("%1 次下载").arg(m_info.plugin.downloads),
        }.join(QStringLiteral(" · "));
        auto *pluginMeta = new QLabel(metaBits, m_heroCard);
        pluginMeta->setObjectName(QStringLiteral("forumPluginHeroMeta"));
        pluginMeta->setWordWrap(true);
        textColumn->addWidget(pluginMeta);

        if (!m_info.plugin.description.isEmpty())
        {
            auto *description = new QLabel(m_info.plugin.description, m_heroCard);
            description->setObjectName(QStringLiteral("forumPluginHeroDesc"));
            description->setWordWrap(true);
            description->setTextInteractionFlags(Qt::TextSelectableByMouse);
            textColumn->addWidget(description);
        }

        headerRow->addLayout(textColumn, 1);
        m_heroBody->addLayout(headerRow);
        return;
    }

    // ── 普通帖：标题 + 分享 / 收藏（收藏不占元信息行，避免时间/浏览数被挤换行）──
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    auto *title = new QLabel(m_info.title, m_heroCard);
    title->setObjectName(QStringLiteral("forumHeroTitle"));
    title->setWordWrap(true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleRow->addWidget(title, 1);
    titleRow->addWidget(makeShare(), 0, Qt::AlignTop);
    titleRow->addWidget(makeBookmark(), 0, Qt::AlignTop);
    titleRow->addWidget(makeTip(), 0, Qt::AlignTop);

    m_heroBody->addLayout(titleRow);
}

void ForumThreadDetailPage::appendHeroMeta()
{
    // ── 元信息：板块 · 作者 · 时间 · 浏览 · 回复 ──
    auto *metaRow = new QHBoxLayout();
    metaRow->setSpacing(10);
    metaRow->setContentsMargins(0, 6, 0, 0);

    // 插件帖 / 投影帖 / 教程帖：先挂一枚徽章，一眼看出这帖是什么类型
    if (m_info.plugin.isValid())
        metaRow->addWidget(ForumKit::makeBadge(m_heroCard, tr("插件")));
    else if (m_info.schematic.isValid())
        metaRow->addWidget(ForumKit::makeBadge(m_heroCard, tr("投影")));
    else if (isTutorialThread())
        metaRow->addWidget(ForumKit::makeBadge(m_heroCard, tr("教程")));

    if (!m_info.boardSlug.isEmpty())
    {
        auto *boardChip = new QPushButton(m_info.boardName, m_heroCard);
        boardChip->setObjectName(QStringLiteral("forumBoardChip"));
        boardChip->setFlat(true);
        boardChip->setCursor(Qt::PointingHandCursor);
        boardChip->setFixedHeight(24);
        const QString slug = m_info.boardSlug;
        const QString boardName = m_info.boardName;
        connect(boardChip, &QPushButton::clicked, this,
                [this, slug, boardName]() { emit boardOpenRequested(slug, boardName); });
        metaRow->addWidget(boardChip);
    }

    if (m_info.authorId > 0)
    {
        // 作者做成一颗小胶囊（头像 + 昵称），整块都能点进主页：
        // 比头像旁边裸放一行名字更像「一个人」，也把名字和时间分开
        auto *chip = new ForumKit::ClickableCard(m_heroCard);
        chip->setObjectName(QStringLiteral("forumAuthorChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedHeight(28);
        const int authorId = m_info.authorId;
        chip->onClick = [this, authorId]() { emit userOpenRequested(authorId); };

        auto *chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(3, 2, 11, 2);
        chipLayout->setSpacing(7);
        chipLayout->addWidget(ForumKit::makeAvatar(chip, m_info.authorAvatar,
                                                   m_info.authorName, 22));
        auto *authorLabel = new QLabel(m_info.authorName, chip);
        authorLabel->setObjectName(QStringLiteral("forumAuthorChipName"));
        chipLayout->addWidget(authorLabel);
        metaRow->addWidget(chip);
    }

    auto *meta = new QLabel(
        tr("发布于 %1 · %2 次浏览 · %3 条回复")
            .arg(absoluteTime(m_info.createdAt).isEmpty() ? ForumKit::relativeTime(m_info.createdAt)
                                                          : absoluteTime(m_info.createdAt))
            .arg(m_info.views)
            .arg(m_info.replies),
        m_heroCard);
    meta->setObjectName(QStringLiteral("forumMuted"));
    metaRow->addWidget(meta);

    metaRow->addStretch(1);

    m_heroBody->addLayout(metaRow);
}

void ForumThreadDetailPage::appendHeroTags()
{
    // ── 标签 ──
    if (!m_info.tags.isEmpty())
    {
        auto *tagsRow = new QHBoxLayout();
        tagsRow->setSpacing(6);
        for (const QString &tag : m_info.tags)
        {
            auto *chip = ForumKit::makeTagChip(m_heroCard, tag);
            const QString tagValue = tag;
            connect(chip, &QPushButton::clicked, this,
                    [this, tagValue]() { emit tagOpenRequested(tagValue); });
            tagsRow->addWidget(chip);
        }
        tagsRow->addStretch(1);
        m_heroBody->addLayout(tagsRow);
    }
}

void ForumThreadDetailPage::appendHeroAdmin()
{
    // ── 管理行（仅作者与管理员看得到） ──
    auto *account = ForumAccountService::instance();
    const bool admin = account->isLoggedIn() && account->user().role == QStringLiteral("admin");
    const bool mine = account->isLoggedIn() && account->user().id == m_info.authorId;
    if (!admin && !mine)
        return;

    auto *adminRow = new QHBoxLayout();
    adminRow->setSpacing(8);
    adminRow->addStretch(1);

    if (admin)
    {
        appendActButton(adminRow, m_info.pinned ? tr("取消置顶") : tr("置顶"), false,
                        [this]() { togglePin(); });
        appendActButton(adminRow, m_info.locked ? tr("取消锁定") : tr("锁定"), false,
                        [this]() { toggleLock(); });
    }
    if (mine)
    {
        appendActButton(adminRow, tr("编辑标题与标签"), false,
                        [this]() { editThreadMeta(); });
    }
    appendActButton(adminRow, tr("删除主题"), true, [this]() { confirmDeleteThread(); });
    m_heroBody->addLayout(adminRow);
}

// ────────────────────────────────────────────────────────────── 插件帖

void ForumThreadDetailPage::appendPluginDownloadCard()
{
    const ForumPluginInfo &plugin = m_info.plugin;

    auto *section = new QFrame(m_railHost);
    section->setObjectName(QStringLiteral("forumPluginSection"));
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("下载"), section);
    title->setObjectName(QStringLiteral("forumPluginSectionTitle"));
    layout->addWidget(title);

    // 主按钮与模组详情页的左栏一致：整块拉满、带下载图标
    auto *installButton = new QPushButton(tr("下载并安装"), section);
    installButton->setObjectName(QStringLiteral("forumPluginInstallBtn"));
    installButton->setCursor(Qt::PointingHandCursor);
    installButton->setMinimumHeight(42);
    installButton->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/download.svg"), QColor("#ffffff"), 18));
    installButton->setIconSize(QSize(18, 18));
    connect(installButton, &QPushButton::clicked,
            this, &ForumThreadDetailPage::downloadAndInstallPlugin);
    layout->addWidget(installButton);

    // 作者可以在这里传新版本：旧包自动挪进历史版本留档
    auto *account = ForumAccountService::instance();
    const bool mine = account->isLoggedIn() && account->user().id == m_info.authorId;
    if (mine)
    {
        auto *uploadButton = new QPushButton(tr("上传新版本"), section);
        uploadButton->setObjectName(QStringLiteral("forumGhostButton"));
        uploadButton->setCursor(Qt::PointingHandCursor);
        uploadButton->setMinimumHeight(34);
        connect(uploadButton, &QPushButton::clicked,
                this, &ForumThreadDetailPage::uploadNewPluginVersion);
        layout->addWidget(uploadButton);
    }

    auto *hint = new QLabel(tr("下载后会先做安全检查，确认无误才装进启动器"), section);
    hint->setObjectName(QStringLiteral("forumPluginHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *sizeLine = new QLabel(tr("%1 · 已下载 %2 次")
                                    .arg(ForumKit::humanFileSize(plugin.fileSize))
                                    .arg(plugin.downloads),
                                section);
    sizeLine->setObjectName(QStringLiteral("forumPluginHint"));
    sizeLine->setWordWrap(true);
    layout->addWidget(sizeLine);

    // 下载 / 安装的进度与结果（平时不占位）
    auto *status = new QLabel(section);
    status->setObjectName(QStringLiteral("forumPluginStatus"));
    status->setWordWrap(true);
    status->hide();
    layout->addWidget(status);

    m_railLayout->addWidget(section);
    m_pluginInstallButton = installButton;
    m_pluginStatusLabel = status;
}

void ForumThreadDetailPage::appendPluginInfoCard()
{
    const ForumPluginInfo &plugin = m_info.plugin;

    auto *section = new QFrame(m_railHost);
    section->setObjectName(QStringLiteral("forumPluginSection"));
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("插件信息"), section);
    title->setObjectName(QStringLiteral("forumPluginSectionTitle"));
    layout->addWidget(title);

    const auto addRow = [section, layout](const QString &key, const QString &value) {
        if (value.isEmpty())
            return;
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        auto *keyLabel = new QLabel(key, section);
        keyLabel->setObjectName(QStringLiteral("forumPluginKey"));
        keyLabel->setFixedWidth(56);
        row->addWidget(keyLabel, 0, Qt::AlignTop);
        auto *valueLabel = new QLabel(value, section);
        valueLabel->setObjectName(QStringLiteral("forumPluginValue"));
        valueLabel->setWordWrap(true);
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->addWidget(valueLabel, 1);
        layout->addLayout(row);
    };

    addRow(tr("版本"), plugin.version.isEmpty() ? tr("未标注") : plugin.version);
    addRow(tr("插件 ID"), plugin.pluginId);
    addRow(tr("作者"), plugin.author.isEmpty() ? tr("未知") : plugin.author);
    addRow(tr("文件大小"), ForumKit::humanFileSize(plugin.fileSize));
    addRow(tr("下载次数"), tr("%1 次").arg(plugin.downloads));
    addRow(tr("发布于"), absoluteTime(m_info.createdAt));

    // 浏览 / 回复：沿用论坛主题信息卡的小方块
    auto *statsRow = new QHBoxLayout();
    statsRow->setSpacing(8);
    const auto addStat = [section, statsRow](const QString &value, const QString &caption) {
        auto *box = new QFrame(section);
        box->setObjectName(QStringLiteral("forumStatBox"));
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(10, 8, 10, 8);
        boxLayout->setSpacing(0);
        auto *valueLabel = new QLabel(value, box);
        valueLabel->setObjectName(QStringLiteral("forumStatValue"));
        boxLayout->addWidget(valueLabel);
        auto *captionLabel = new QLabel(caption, box);
        captionLabel->setObjectName(QStringLiteral("forumRailKey"));
        boxLayout->addWidget(captionLabel);
        statsRow->addWidget(box, 1);
    };
    addStat(QString::number(m_info.views), tr("浏览"));
    addStat(QString::number(m_info.replies), tr("回复"));
    layout->addSpacing(2);
    layout->addLayout(statsRow);

    // 历史版本：新的在前，当前版带标记（列表异步回填）
    if (m_info.plugin.isValid())
    {
        auto *versionsTitle = new QLabel(tr("历史版本"), section);
        versionsTitle->setObjectName(QStringLiteral("forumPluginSectionTitle"));
        layout->addSpacing(6);
        layout->addWidget(versionsTitle);
        m_pluginVersionsLayout = new QVBoxLayout();
        m_pluginVersionsLayout->setContentsMargins(0, 0, 0, 0);
        m_pluginVersionsLayout->setSpacing(4);
        layout->addLayout(m_pluginVersionsLayout);
        loadPluginVersions();
    }

    if (m_info.locked)
    {
        auto *note = new QLabel(tr("该主题已锁定，暂不接受新回复"), section);
        note->setObjectName(QStringLiteral("forumRailNote"));
        note->setWordWrap(true);
        layout->addWidget(note);
    }

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);

    auto *refresh = new QPushButton(tr("刷新本页"), section);
    refresh->setObjectName(QStringLiteral("forumRailButton"));
    refresh->setCursor(Qt::PointingHandCursor);
    refresh->setFixedHeight(30);
    connect(refresh, &QPushButton::clicked, this, [this]() { reload(); });
    buttonRow->addWidget(refresh, 1);

    auto *copy = new QPushButton(tr("复制链接"), section);
    copy->setObjectName(QStringLiteral("forumRailButton"));
    copy->setCursor(Qt::PointingHandCursor);
    copy->setFixedHeight(30);
    connect(copy, &QPushButton::clicked, this, &ForumThreadDetailPage::copyThreadLink);
    buttonRow->addWidget(copy, 1);

    layout->addSpacing(2);
    layout->addLayout(buttonRow);

    m_railLayout->addWidget(section);
}

void ForumThreadDetailPage::appendResourceLinksCard(const QVector<ForumResourceLink> &links)
{
    if (links.isEmpty())
        return;

    auto *section = new QFrame(m_railHost);
    section->setObjectName(QStringLiteral("forumPluginSection"));
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(6);

    auto *title = new QLabel(tr("相关链接"), section);
    title->setObjectName(QStringLiteral("forumPluginSectionTitle"));
    layout->addWidget(title);

    // 作者绑定的官网 / 仓库 / 反馈入口，一颗一行；坏链（非 http(s)）点了不跳
    for (const ForumResourceLink &link : links)
    {
        if (!link.isValid())
            continue;
        auto *button = new QPushButton(link.label, section);
        button->setObjectName(QStringLiteral("forumRailButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(30);
        button->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/external_link.svg"),
            palette().color(QPalette::ButtonText), 14));
        button->setIconSize(QSize(14, 14));
        button->setToolTip(link.url);
        connect(button, &QPushButton::clicked, this, [url = link.url]() {
            const QUrl parsed(url, QUrl::TolerantMode);
            if (parsed.scheme() == QLatin1String("http") || parsed.scheme() == QLatin1String("https"))
                QDesktopServices::openUrl(parsed);
        });
        layout->addWidget(button);
    }

    m_railLayout->addWidget(section);
}

void ForumThreadDetailPage::uploadNewPluginVersion()
{
    if (m_pluginUploadingVersion || m_pluginBusy || !m_info.plugin.isValid())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择新版本插件包"), QString(),
        QStringLiteral("BlockBox 插件 (*.BlockBox *.blockbox)"));
    if (path.isEmpty())
        return;

    constexpr qint64 kMaxPluginBytes = 20 * 1024 * 1024;
    const QFileInfo info(path);
    if (info.size() > kMaxPluginBytes)
    {
        NotificationManager::showInfo(this, tr("插件包超过 20 MB，服务器不收这么大，先删减一下"));
        return;
    }

    // 本地先解析一遍：清单不合法的包传上去也会被服务端退回
    const PluginInfo localInfo = PluginManager::instance()->inspectPluginFile(path);
    if (!localInfo.loaded || localInfo.id.isEmpty() || localInfo.name.isEmpty())
    {
        NotificationManager::showInfo(this, tr("这不是有效的插件包：%1").arg(
            localInfo.loadError.isEmpty() ? tr("清单缺少 id 或 name") : localInfo.loadError));
        return;
    }
    if (localInfo.id != m_info.plugin.pluginId)
    {
        NotificationManager::showInfo(
            this, tr("这个包的插件 ID（%1）和当前作品（%2）不是同一个插件")
                      .arg(localInfo.id, m_info.plugin.pluginId));
        return;
    }

    m_pluginUploadingVersion = true;
    if (m_pluginInstallButton)
        m_pluginInstallButton->setEnabled(false);
    if (m_pluginStatusLabel)
    {
        m_pluginStatusLabel->setText(tr("正在上传新版本…"));
        m_pluginStatusLabel->show();
    }

    const auto restore = [this]() {
        m_pluginUploadingVersion = false;
        if (m_pluginInstallButton)
            m_pluginInstallButton->setEnabled(true);
    };

    ForumContentService::instance()->uploadPluginVersion(
        m_threadId, path,
        [this](int percent) {
            if (m_pluginStatusLabel)
            {
                m_pluginStatusLabel->setText(tr("正在上传新版本 %1%").arg(percent));
                m_pluginStatusLabel->show();
            }
        },
        [this, restore]() {
            restore();
            NotificationManager::showSuccess(this, tr("新版本已上传，旧版本留在历史版本里"));
            reload();
        },
        [this, restore](const QString &message, int) {
            restore();
            if (m_pluginStatusLabel)
            {
                m_pluginStatusLabel->setText(message);
                m_pluginStatusLabel->show();
            }
        });
}

void ForumThreadDetailPage::loadPluginVersions()
{
    if (m_threadId <= 0 || !m_info.plugin.isValid())
        return;

    ForumContentService::instance()->fetchPluginVersions(
        m_threadId,
        [this](const QVector<ForumPluginVersionInfo> &versions) {
            // 侧栏可能已经重建：布局指针作废就丢掉这次结果
            if (!m_pluginVersionsLayout)
                return;
            if (versions.isEmpty())
            {
                auto *empty =
                    ForumKit::makeMuted(m_pluginVersionsLayout->parentWidget(), tr("还没有历史版本"));
                m_pluginVersionsLayout->addWidget(empty);
                return;
            }
            QWidget *section = m_pluginVersionsLayout->parentWidget();
            for (const ForumPluginVersionInfo &version : versions)
            {
                QStringList parts;
                parts << (version.version.isEmpty() ? tr("未标注版本")
                                                    : tr("v%1").arg(version.version));
                parts << ForumKit::humanFileSize(version.fileSize);
                parts << tr("已下载 %1 次").arg(version.downloads);
                if (version.createdAt > 0)
                    parts << absoluteTime(version.createdAt);

                auto *row = new QHBoxLayout();
                row->setSpacing(6);
                auto *text = new QLabel(parts.join(QStringLiteral(" · ")), section);
                text->setObjectName(QStringLiteral("forumPluginHint"));
                text->setWordWrap(true);
                row->addWidget(text, 1);
                if (version.current)
                    row->addWidget(ForumKit::makeBadge(section, tr("当前版")));
                m_pluginVersionsLayout->addLayout(row);
            }
        },
        [](const QString &, int) {
            // 历史版本是附加信息，拉不到就不显示，不打扰正文
        });
}

void ForumThreadDetailPage::downloadAndInstallPlugin()
{
    if (m_pluginBusy || !m_info.plugin.isValid())
        return;
    m_pluginBusy = true;
    if (m_pluginInstallButton)
        m_pluginInstallButton->setEnabled(false);
    if (m_pluginStatusLabel)
    {
        m_pluginStatusLabel->setText(tr("正在下载…"));
        m_pluginStatusLabel->show();
    }

    const auto restore = [this]() {
        m_pluginBusy = false;
        if (m_pluginInstallButton)
            m_pluginInstallButton->setEnabled(true);
    };
    const auto progress = [this](int percent) {
        if (m_pluginStatusLabel)
        {
            m_pluginStatusLabel->setText(tr("正在下载 %1%").arg(percent));
            m_pluginStatusLabel->show();
        }
    };

    ForumContentService::instance()->fetchPluginDownload(
        m_threadId,
        [this, restore, progress](const ForumPluginDownload &download) {
            if (!download.isValid())
            {
                restore();
                setStatus(tr("拿不到插件下载地址，稍后再试"), true);
                return;
            }

            // 落到临时目录：导入成功与否都不留垃圾
            const QString dir = QDir::tempPath() + QStringLiteral("/BlockBox/forum-plugin");
            QDir().mkpath(dir);
            QString target = dir + QLatin1Char('/') + download.filename;
            if (!target.endsWith(QLatin1String(".BlockBox"), Qt::CaseInsensitive))
                target += QStringLiteral(".BlockBox");

            auto *manager = new QNetworkAccessManager(this);
            QNetworkRequest request{QUrl(download.url)};
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
            request.setTransferTimeout(120000);

            auto *reply = manager->get(request);
            connect(reply, &QNetworkReply::downloadProgress, this, [progress](qint64 received, qint64 total) {
                if (total > 0)
                    progress(int(received * 100 / total));
            });
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, manager, target, restore]() {
                        reply->deleteLater();
                        manager->deleteLater();
                        const QByteArray data = reply->readAll();
                        if (reply->error() != QNetworkReply::NoError || data.isEmpty())
                        {
                            restore();
                            setStatus(tr("插件包下载失败，请检查网络后再试"), true);
                            return;
                        }
                        QFile file(target);
                        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
                        {
                            restore();
                            setStatus(tr("插件包写不进临时目录"), true);
                            return;
                        }
                        file.close();

                        // 与「导入插件」完全一致：解析→信任确认→导入（安全闸不跳过）
                        PluginInfo info = PluginManager::instance()->inspectPluginFile(target);
                        if (!info.loaded)
                        {
                            restore();
                            setStatus(tr("下载的文件不是有效插件包"), true);
                            return;
                        }
                        const QString scripts = PluginManager::instance()->collectScriptsContent(target);
                        if (!PluginSafetyGuard::confirmBeforeInstall(
                                window(), info.name, info.author, info.version,
                                info.permissions, PluginSafetyGuard::RiskMedium,
                                QString(),   // 顶层清单没有风险说明字段，风险由扫描/权限综合判定
                                scripts))
                        {
                            restore();
                            setStatus(tr("已取消安装"));
                            return;
                        }
                        QString error;
                        if (!PluginManager::instance()->importPlugin(target, &error))
                        {
                            restore();
                            setStatus(tr("插件安装失败：%1").arg(error), true);
                            return;
                        }
                        PluginManager::instance()->refresh();
                        emit pluginInstalled();
                        if (m_pluginStatusLabel)
                        {
                            m_pluginStatusLabel->setText(tr("安装成功！到「插件」列表里查看"));
                            m_pluginStatusLabel->show();
                        }
                        restore();
                    });
        },
        [this, restore](const QString &message, int) {
            restore();
            setStatus(message, true);
        });
}

void ForumThreadDetailPage::appendSchematicDownloadCard()
{
    const ForumSchematicInfo &schematic = m_info.schematic;

    auto *section = new QFrame(m_railHost);
    section->setObjectName(QStringLiteral("forumPluginSection"));
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("下载"), section);
    title->setObjectName(QStringLiteral("forumPluginSectionTitle"));
    layout->addWidget(title);

    // 与插件帖下载卡同一套观感：整块拉满、带下载图标
    auto *downloadButton = new QPushButton(tr("下载投影"), section);
    downloadButton->setObjectName(QStringLiteral("forumPluginInstallBtn"));
    downloadButton->setCursor(Qt::PointingHandCursor);
    downloadButton->setMinimumHeight(42);
    downloadButton->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/download.svg"), QColor("#ffffff"), 18));
    downloadButton->setIconSize(QSize(18, 18));
    connect(downloadButton, &QPushButton::clicked,
            this, &ForumThreadDetailPage::downloadSchematic);
    layout->addWidget(downloadButton);

    auto *hint = new QLabel(tr("下载后放进实例的 schematics 文件夹，Litematica 就能加载"), section);
    hint->setObjectName(QStringLiteral("forumPluginHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *sizeLine = new QLabel(tr("%1 · 已下载 %2 次")
                                    .arg(ForumKit::humanFileSize(schematic.fileSize))
                                    .arg(schematic.downloads),
                                section);
    sizeLine->setObjectName(QStringLiteral("forumPluginHint"));
    sizeLine->setWordWrap(true);
    layout->addWidget(sizeLine);

    // 下载的进度与结果（平时不占位）
    auto *status = new QLabel(section);
    status->setObjectName(QStringLiteral("forumPluginStatus"));
    status->setWordWrap(true);
    status->hide();
    layout->addWidget(status);

    m_railLayout->addWidget(section);
    m_schematicDownloadButton = downloadButton;
    m_schematicStatusLabel = status;
}

void ForumThreadDetailPage::downloadSchematic()
{
    if (m_schematicBusy || !m_info.schematic.isValid())
        return;
    m_schematicBusy = true;
    if (m_schematicDownloadButton)
        m_schematicDownloadButton->setEnabled(false);
    if (m_schematicStatusLabel)
    {
        m_schematicStatusLabel->setText(tr("正在获取下载地址…"));
        m_schematicStatusLabel->show();
    }

    const auto restore = [this]() {
        m_schematicBusy = false;
        if (m_schematicDownloadButton)
            m_schematicDownloadButton->setEnabled(true);
    };

    ForumContentService::instance()->fetchSchematicDownload(
        m_threadId,
        [this, restore](const ForumSchematicDownload &download) {
            if (!download.isValid())
            {
                restore();
                setStatus(tr("拿不到投影下载地址，稍后再试"), true);
                return;
            }

            // 存哪儿由用户自己挑：放实例 schematics 目录或任意位置都行
            const QString defaultName = download.filename.isEmpty()
                                            ? QStringLiteral("projection.litematic")
                                            : download.filename;
            const QString target = QFileDialog::getSaveFileName(
                window(), tr("保存投影文件"), defaultName);
            if (target.isEmpty())
            {
                restore();
                if (m_schematicStatusLabel)
                {
                    m_schematicStatusLabel->setText(tr("已取消保存"));
                    m_schematicStatusLabel->show();
                }
                return;
            }

            if (m_schematicStatusLabel)
            {
                m_schematicStatusLabel->setText(tr("正在下载…"));
                m_schematicStatusLabel->show();
            }

            auto *manager = new QNetworkAccessManager(this);
            QNetworkRequest request{QUrl(download.url)};
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
            request.setTransferTimeout(300000);

            auto *reply = manager->get(request);
            connect(reply, &QNetworkReply::downloadProgress, this,
                    [this](qint64 received, qint64 total) {
                        if (m_schematicStatusLabel && total > 0)
                        {
                            m_schematicStatusLabel->setText(
                                tr("正在下载 %1%").arg(int(received * 100 / total)));
                            m_schematicStatusLabel->show();
                        }
                    });
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, manager, target, restore]() {
                        reply->deleteLater();
                        manager->deleteLater();
                        const QByteArray data = reply->readAll();
                        if (reply->error() != QNetworkReply::NoError || data.isEmpty())
                        {
                            restore();
                            setStatus(tr("投影下载失败，请检查网络后再试"), true);
                            return;
                        }
                        QFile file(target);
                        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
                        {
                            restore();
                            setStatus(tr("投影文件写不进所选位置"), true);
                            return;
                        }
                        file.close();
                        restore();
                        if (m_schematicStatusLabel)
                        {
                            m_schematicStatusLabel->setText(tr("已保存，放进实例的 schematics 文件夹就能用"));
                            m_schematicStatusLabel->show();
                        }
                    });
        },
        [this, restore](const QString &message, int) {
            restore();
            setStatus(message, true);
        });
}

// ────────────────────────────────────────────────────────────── 楼层

void ForumThreadDetailPage::renderPosts(const QVector<ForumPost> &posts)
{
    ForumKit::clearLayout(m_floorsLayout);
    m_postCards.clear();

    if (posts.isEmpty())
    {
        m_floorsLayout->addWidget(ForumKit::makeMuted(m_mainColumn, tr("这一页没有楼层")));
        return;
    }

    for (const ForumPost &post : posts)
        m_floorsLayout->addWidget(buildPostRow(post));
}

QWidget *ForumThreadDetailPage::buildPostRow(const ForumPost &post)
{
    // 已删除楼层：网页版只是一张虚线卡 + 一行灰字，不再渲染作者栏
    if (post.deleted)
    {
        auto *gone = new QFrame(m_mainColumn);
        gone->setObjectName(QStringLiteral("forumPost"));
        gone->setProperty("deleted", true);
        auto *goneLayout = new QVBoxLayout(gone);
        goneLayout->setContentsMargins(20, 14, 20, 14);
        goneLayout->setSpacing(6);

        auto *goneHead = new QHBoxLayout();
        goneHead->setSpacing(8);
        auto *mark = new QLabel(tr("该楼层已被作者或管理员删除"), gone);
        mark->setObjectName(QStringLiteral("forumDeletedMark"));
        goneHead->addWidget(mark);
        goneHead->addStretch(1);
        auto *floorLabel = new QLabel(tr("%1 楼").arg(post.floor), gone);
        floorLabel->setObjectName(QStringLiteral("forumFloorNo"));
        goneHead->addWidget(floorLabel);
        goneLayout->addLayout(goneHead);
        return gone;
    }

    auto *card = new QFrame(m_mainColumn);
    card->setObjectName(QStringLiteral("forumPost"));
    card->setProperty("op", post.floor == 1);

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(20, 18, 20, 16);
    row->setSpacing(18);

    // ── 左：作者栏 ──
    auto *side = new QWidget(card);
    side->setObjectName(QStringLiteral("forumPostSide"));
    side->setFixedWidth(112);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 2, 0, 0);
    sideLayout->setSpacing(7);

    const QString name = authorDisplay(post.author);
    // 头像外面套一圈主色描边，和网页版 .post-side .avatar 的柔光圈一致
    auto *ring = new QFrame(side);
    ring->setObjectName(QStringLiteral("forumAvatarRing"));
    ring->setFixedSize(60, 60);
    auto *ringLayout = new QVBoxLayout(ring);
    ringLayout->setContentsMargins(4, 4, 4, 4);
    ringLayout->setSpacing(0);
    ringLayout->addWidget(ForumKit::makeAvatar(ring, post.author.avatar, name, 52));
    sideLayout->addWidget(ring, 0, Qt::AlignHCenter);

    auto *nameButton = new QPushButton(name, side);
    nameButton->setObjectName(QStringLiteral("forumPostName"));
    nameButton->setFlat(true);
    nameButton->setCursor(Qt::PointingHandCursor);
    nameButton->setToolTip(name);
    const int authorId = post.author.id;
    connect(nameButton, &QPushButton::clicked, this,
            [this, authorId]() { if (authorId > 0) emit userOpenRequested(authorId); });
    sideLayout->addWidget(nameButton);

    if (post.author.role == QStringLiteral("admin"))
        sideLayout->addWidget(ForumKit::makeBadge(side, tr("管理员")), 0, Qt::AlignHCenter);

    QStringList counts;
    counts << tr("%1 帖 · %2 主题").arg(post.author.postCount).arg(post.author.threadCount);
    const QString joined = joinedText(post.author);
    if (!joined.isEmpty())
        counts << joined;
    auto *countLabel = new QLabel(counts.join(QLatin1Char('\n')), side);
    countLabel->setObjectName(QStringLiteral("forumPostCounts"));
    countLabel->setAlignment(Qt::AlignHCenter);
    countLabel->setWordWrap(true);
    sideLayout->addWidget(countLabel);
    sideLayout->addStretch(1);

    row->addWidget(side, 0, Qt::AlignTop);

    // ── 右：正文 ──
    auto *body = new QWidget(card);
    body->setObjectName(QStringLiteral("forumPostBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // 楼层头：楼主 / 时间 / 已编辑 … 楼层号
    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(8);
    if (post.floor == 1)
        headRow->addWidget(ForumKit::makeBadge(body, tr("楼主")));

    const QString relative = ForumKit::relativeTime(post.createdAt);
    auto *timeLabel = new QLabel(relative.isEmpty() ? absoluteTime(post.createdAt) : relative, body);
    timeLabel->setObjectName(QStringLiteral("forumPostTime"));
    timeLabel->setToolTip(absoluteTime(post.createdAt));
    headRow->addWidget(timeLabel);

    if (post.editCount > 0)
    {
        auto *edited = new QLabel(tr("已编辑"), body);
        edited->setObjectName(QStringLiteral("forumEditMark"));
        headRow->addWidget(edited);
    }

    headRow->addStretch(1);

    auto *floorLabel = new QLabel(tr("%1 楼").arg(post.floor), body);
    floorLabel->setObjectName(QStringLiteral("forumFloorNo"));
    headRow->addWidget(floorLabel);
    bodyLayout->addLayout(headRow);

    // 正文
    auto *contentHost = new QWidget(body);
    contentHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 10, 0, 0);
    contentLayout->setSpacing(0);
    if (post.deleted)
    {
        contentLayout->addWidget(ForumKit::makeMuted(contentHost, tr("该楼层已被作者或管理员删除")));
    }
    else
    {
        auto *browser = new ForumPostBrowser(contentHost);
        browser->setPostMarkdown(post.content);
        contentLayout->addWidget(browser);

        // 正文图片点开看大图；帖子内链在启动器内跳转，外部链接才开浏览器
        connect(browser, &ForumPostBrowser::imageClicked, this, [](const QString &path) {
            ForumPostBrowser::showImagePreview(path);
        });
        connect(browser, &ForumPostBrowser::linkClicked, this,
                &ForumThreadDetailPage::handleContentLink);
        m_postCards.insert(post.id, CardRef{card, browser});
    }
    bodyLayout->addWidget(contentHost);

    // 楼中楼：一张浅底内嵌面板，把回复和楼层正文清楚分开
    if (!post.children.isEmpty())
    {
        auto *children = new QFrame(body);
        children->setObjectName(QStringLiteral("forumChildren"));
        auto *childrenLayout = new QVBoxLayout(children);
        childrenLayout->setContentsMargins(14, 4, 14, 8);
        childrenLayout->setSpacing(0);

        auto *visibleHost = new QWidget(children);
        auto *visibleLayout = new QVBoxLayout(visibleHost);
        visibleLayout->setContentsMargins(0, 0, 0, 0);
        visibleLayout->setSpacing(0);

        auto *hiddenHost = new QWidget(children);
        hiddenHost->setObjectName(QStringLiteral("forumChildrenMore"));
        auto *hiddenLayout = new QVBoxLayout(hiddenHost);
        hiddenLayout->setContentsMargins(0, 0, 0, 0);
        hiddenLayout->setSpacing(0);
        hiddenHost->hide();

        for (int i = 0; i < post.children.size(); ++i)
        {
            auto *child = static_cast<QWidget *>(buildChildRow(post.children.at(i)));
            // 首条不画虚线分隔，折叠块的第一条也不画（由折叠块自己画顶线）
            child->setProperty("first", i == 0 || i == kVisibleChildren);
            if (i < kVisibleChildren)
                visibleLayout->addWidget(child);
            else
                hiddenLayout->addWidget(child);
        }

        childrenLayout->addWidget(visibleHost);
        childrenLayout->addWidget(hiddenHost);

        if (post.children.size() > kVisibleChildren)
        {
            auto *toggle = makeAct(children, tr("查看全部 %1 条回复").arg(post.children.size()));
            toggle->setObjectName(QStringLiteral("forumChildrenToggle"));
            toggle->setFixedHeight(28);
            connect(toggle, &QPushButton::clicked, children, [toggle, hiddenHost]() {
                const bool expand = hiddenHost->isHidden();
                hiddenHost->setVisible(expand);
                if (expand)
                    toggle->hide();
            });
            childrenLayout->addWidget(toggle, 0, Qt::AlignLeft);
        }

        bodyLayout->addWidget(children);
    }

    // 操作行：上方一条虚线分隔，按钮做成小胶囊（网页版 .post-actions 同款）
    auto *actionsHost = new QWidget(body);
    actionsHost->setObjectName(QStringLiteral("forumPostActions"));
    auto *actionsOuter = new QVBoxLayout(actionsHost);
    actionsOuter->setContentsMargins(0, 0, 0, 0);
    actionsOuter->setSpacing(0);

    auto *divider = new QFrame(actionsHost);
    divider->setObjectName(QStringLiteral("forumActDivider"));
    divider->setFixedHeight(1);
    actionsOuter->addSpacing(12);
    actionsOuter->addWidget(divider);

    auto *actions = new QHBoxLayout();
    actions->setContentsMargins(0, 10, 0, 0);
    actions->setSpacing(6);
    actionsOuter->addLayout(actions);
    appendPostActions(actions, post, false);
    bodyLayout->addWidget(actionsHost);

    row->addWidget(body, 1);
    setupCardMenu(card, post);
    return card;
}

QWidget *ForumThreadDetailPage::buildChildRow(const ForumPost &child)
{
    auto *row = new QWidget(m_mainColumn);
    row->setObjectName(QStringLiteral("forumChild"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(1, 10, 1, 10);
    layout->setSpacing(10);

    const QString name = authorDisplay(child.author);
    layout->addWidget(ForumKit::makeAvatar(row, child.author.avatar, name, 26), 0, Qt::AlignTop);

    auto *body = new QWidget(row);
    body->setAttribute(Qt::WA_StyledBackground, false);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(8);
    auto *nameButton = new QPushButton(name, body);
    nameButton->setObjectName(QStringLiteral("forumChildName"));
    nameButton->setFlat(true);
    nameButton->setCursor(Qt::PointingHandCursor);
    const int authorId = child.author.id;
    connect(nameButton, &QPushButton::clicked, this,
            [this, authorId]() { if (authorId > 0) emit userOpenRequested(authorId); });
    headRow->addWidget(nameButton);

    if (child.author.role == QStringLiteral("admin"))
        headRow->addWidget(ForumKit::makeBadge(body, tr("管理员")));

    auto *timeLabel = new QLabel(ForumKit::relativeTime(child.createdAt), body);
    timeLabel->setObjectName(QStringLiteral("forumChildTime"));
    headRow->addWidget(timeLabel);
    headRow->addStretch(1);

    auto *floorLabel = new QLabel(QStringLiteral("#%1").arg(child.floor), body);
    floorLabel->setObjectName(QStringLiteral("forumFloorNo"));
    headRow->addWidget(floorLabel);
    bodyLayout->addLayout(headRow);

    if (child.deleted)
    {
        bodyLayout->addWidget(ForumKit::makeMuted(body, tr("该楼层已删除")));
    }
    else
    {
        auto *browser = new ForumPostBrowser(body);
        // 楼中楼用紧凑排版：字号略小、段间距收紧，一眼能和楼层正文分开
        browser->setCompactMode(true);
        browser->setPostMarkdown(child.content);
        bodyLayout->addWidget(browser);
        connect(browser, &ForumPostBrowser::imageClicked, this, [](const QString &path) {
            ForumPostBrowser::showImagePreview(path);
        });
        connect(browser, &ForumPostBrowser::linkClicked, this,
                &ForumThreadDetailPage::handleContentLink);
        m_postCards.insert(child.id, CardRef{row, browser});
    }

    auto *actions = new QHBoxLayout();
    actions->setContentsMargins(0, 4, 0, 0);
    actions->setSpacing(6);
    if (!child.deleted)
        appendPostActions(actions, child, true);
    bodyLayout->addLayout(actions);

    layout->addWidget(body, 1);
    setupCardMenu(row, child);
    return row;
}

void ForumThreadDetailPage::appendPostActions(QHBoxLayout *row, const ForumPost &post, bool isChild)
{
    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool admin = loggedIn && account->user().role == QStringLiteral("admin");
    const bool mine = loggedIn && account->user().id == post.author.id;
    const bool canEdit = loggedIn && !post.deleted && (mine || admin);

    if (!post.deleted)
    {
        appendActButton(row, tr("回复"), false, [this, post]() {
            if (!ForumAccountService::instance()->isLoggedIn())
            {
                emit loginRequested();
                return;
            }
            if (!ForumAccountService::instance()->hasPassedQuiz())
            {
                openQuizDialog();
                return;
            }
            setReplyTarget(post);
            scrollToComposer();
        });
        appendActButton(row, tr("引用"), false, [this, post]() { loadQuote(post); });

        // 点赞：心形 SVG，点过之后变红，点下去有放大回弹 + 小红点四散
        auto *likeButton = new ForumKit::LikeButton(this);
        likeButton->setProperty("postId", post.id);
        likeButton->setLikes(post.likes);
        likeButton->setLiked(post.liked);
        likeButton->setToolTip(post.liked ? tr("取消点赞") : tr("给这一楼点个赞"));
        connect(likeButton, &QPushButton::clicked, this, [this, likeButton]() {
            if (!ForumAccountService::instance()->isLoggedIn())
            {
                emit loginRequested();
                return;
            }
            const int postId = likeButton->property("postId").toInt();
            likeButton->setEnabled(false);
            ForumContentService::instance()->togglePostLike(
                postId,
                [likeButton](bool liked, int likes) {
                    likeButton->setEnabled(true);
                    likeButton->setLikes(likes);
                    likeButton->setLiked(liked, true);
                    likeButton->setToolTip(liked ? tr("取消点赞") : tr("给这一楼点个赞"));
                },
                [this, likeButton](const QString &message, int) {
                    likeButton->setEnabled(true);
                    NotificationManager::showError(this, message);
                });
        });
        row->addWidget(likeButton);

        appendActButton(row, tr("复制链接"), false,
                        [this, post]() { copyPostLink(post); });
    }

    if (isChild)
    {
        if (!post.deleted && !mine && loggedIn)
            appendActButton(row, tr("举报"), false, [this, post]() { reportPost(post); });
        if (canEdit)
            appendActButton(row, tr("删除"), true, [this, post]() { confirmDeletePost(post); });
        row->addStretch(1);
        return;
    }

    row->addStretch(1);

    if (canEdit)
        appendActButton(row, tr("编辑"), false, [this, post]() { startEditPost(post); });
    if (!post.deleted && !mine && loggedIn)
        appendActButton(row, tr("举报"), false, [this, post]() { reportPost(post); });
    if (canEdit)
        appendActButton(row, tr("删除"), true, [this, post]() { confirmDeletePost(post); });
}

void ForumThreadDetailPage::appendActButton(QHBoxLayout *row, const QString &text, bool danger,
                                            std::function<void()> onClick)
{
    auto *button = makeAct(this, text, danger);
    connect(button, &QPushButton::clicked, this, [onClick]() { onClick(); });
    row->addWidget(button);
}

void ForumThreadDetailPage::setupCardMenu(QWidget *card, const ForumPost &post)
{
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, card, [this, card, post](const QPoint &pos) {
        auto *account = ForumAccountService::instance();
        QMenu menu(card);
        const bool mine = account->isLoggedIn() && account->user().id == post.author.id;
        const bool admin = account->isLoggedIn() && account->user().role == QStringLiteral("admin");

        if (mine || admin)
        {
            menu.addAction(tr("编辑这条回复"), this, [this, post]() { startEditPost(post); });
            menu.addAction(tr("删除这条回复"), this, [this, post]() { confirmDeletePost(post); });
        }
        else if (account->isLoggedIn())
        {
            menu.addAction(tr("举报"), this, [this, post]() { reportPost(post); });
        }
        else
        {
            return;
        }

        menu.exec(card->mapToGlobal(pos));
    });
}

// ────────────────────────────────────────────────────────────── 详细信息（资源帖）

void ForumThreadDetailPage::renderBodyCard()
{
    if (!m_bodyCard || !m_bodyBrowser || !m_bodyTitle)
        return;

    // 没写正文的老帖（光秃秃只有下载）不摆一张空卡；教程帖的正文卡叫「教程正文」
    const bool hasBody = (isResourceThread() || isTutorialThread()) && !m_bodyMarkdown.isEmpty();
    m_bodyTitle->setText(isTutorialThread() ? tr("教程正文") : tr("详细信息"));
    m_bodyTitle->setVisible(hasBody);
    m_bodyCard->setVisible(hasBody);
    if (hasBody)
    {
        m_bodyBrowser->setPostMarkdown(m_bodyMarkdown);
        // 1 楼正文渲染完成后右栏才有目录可列；清掉旧目录卡，重铺右栏
        renderRail();
    }
}

// ────────────────────────────────────────────────────────────── 评论（资源帖）

void ForumThreadDetailPage::updateCommentHeader()
{
    if (!m_discussionCount)
        return;
    if (!isResourceThread())
    {
        m_discussionCount->clear();
        return;
    }
    const int total = m_commentTotal > 0 ? m_commentTotal : m_info.comments;
    m_discussionCount->setText(tr("%1 条评论").arg(total));
}

void ForumThreadDetailPage::loadComments(int page)
{
    if (m_threadId <= 0 || !isResourceThread())
        return;

    setStatus(tr("正在加载评论…"));
    ForumContentService::instance()->fetchComments(
        m_threadId, qMax(1, page),
        [this](const QVector<ForumComment> &comments, const ForumPagination &pagination) {
            m_commentPage = qMax(1, pagination.page);
            m_commentPages = qMax(1, pagination.pages);
            m_commentTotal = pagination.total;
            // 刷新本页 / 重新进入时按评论页还原，别被楼层页码带回去
            m_page = m_commentPage;
            setStatus(QString());
            renderComments(comments);
            m_pager->setPage(m_commentPage, m_commentPages);
            updateCommentHeader();
            m_scroll->verticalScrollBar()->setValue(0);
        },
        [this](const QString &message, int) {
            setStatus(tr("评论加载失败：%1").arg(message), true);
        });
}

void ForumThreadDetailPage::renderComments(const QVector<ForumComment> &comments)
{
    ForumKit::clearLayout(m_floorsLayout);
    m_postCards.clear();

    if (comments.isEmpty())
    {
        m_floorsLayout->addWidget(
            ForumKit::makeMuted(m_mainColumn, tr("还没有评论，说说你用过之后的感受吧")));
        return;
    }

    for (const ForumComment &comment : comments)
        m_floorsLayout->addWidget(buildCommentRow(comment));
}

QWidget *ForumThreadDetailPage::buildCommentRow(const ForumComment &comment)
{
    auto *card = new QFrame(m_mainColumn);
    card->setObjectName(QStringLiteral("forumComment"));

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(16, 14, 16, 14);
    row->setSpacing(12);

    const QString name = authorDisplay(comment.author);
    row->addWidget(ForumKit::makeAvatar(card, comment.author.avatar, name, 32), 0, Qt::AlignTop);

    auto *body = new QWidget(card);
    body->setAttribute(Qt::WA_StyledBackground, false);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // 头部：昵称 / 身份 / 时间 ……… 点赞
    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(8);

    auto *nameButton = new QPushButton(name, body);
    nameButton->setObjectName(QStringLiteral("forumCommentName"));
    nameButton->setFlat(true);
    nameButton->setCursor(Qt::PointingHandCursor);
    nameButton->setToolTip(name);
    const int authorId = comment.author.id;
    connect(nameButton, &QPushButton::clicked, this,
            [this, authorId]() { if (authorId > 0) emit userOpenRequested(authorId); });
    headRow->addWidget(nameButton);

    if (comment.author.role == QLatin1String("admin"))
        headRow->addWidget(ForumKit::makeBadge(body, tr("管理员")));
    else if (comment.author.role == QLatin1String("moderator"))
        headRow->addWidget(ForumKit::makeBadge(body, tr("次级管理员")));

    const QString relative = ForumKit::relativeTime(comment.createdAt);
    auto *timeLabel = new QLabel(relative.isEmpty() ? absoluteTime(comment.createdAt) : relative, body);
    timeLabel->setObjectName(QStringLiteral("forumCommentTime"));
    timeLabel->setToolTip(absoluteTime(comment.createdAt));
    headRow->addWidget(timeLabel);
    headRow->addStretch(1);

    auto *likeButton = new ForumKit::LikeButton(body);
    likeButton->setLikes(comment.likes);
    likeButton->setLiked(comment.liked);
    likeButton->setToolTip(comment.liked ? tr("取消点赞") : tr("给这条评论点个赞"));
    const int commentId = comment.id;
    connect(likeButton, &QPushButton::clicked, this, [this, likeButton, commentId]() {
        if (!ForumAccountService::instance()->isLoggedIn())
        {
            emit loginRequested();
            return;
        }
        likeButton->setEnabled(false);
        ForumContentService::instance()->toggleCommentLike(
            commentId,
            [likeButton](bool liked, int likes) {
                likeButton->setEnabled(true);
                likeButton->setLikes(likes);
                likeButton->setLiked(liked, true);
                likeButton->setToolTip(liked ? tr("取消点赞") : tr("给这条评论点个赞"));
            },
            [this, likeButton](const QString &message, int) {
                likeButton->setEnabled(true);
                NotificationManager::showError(this, message);
            });
    });
    headRow->addWidget(likeButton);
    bodyLayout->addLayout(headRow);

    // 正文（Markdown 渲染与楼层同一套）
    auto *contentHost = new QWidget(body);
    contentHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 8, 0, 0);
    contentLayout->setSpacing(0);
    auto *browser = new ForumPostBrowser(contentHost);
    browser->setPostMarkdown(comment.content);
    contentLayout->addWidget(browser);
    connect(browser, &ForumPostBrowser::imageClicked, this, [](const QString &path) {
        ForumPostBrowser::showImagePreview(path);
    });
    connect(browser, &ForumPostBrowser::linkClicked, this,
            &ForumThreadDetailPage::handleContentLink);
    bodyLayout->addWidget(contentHost);

    // 操作行：自己的评论能删，别人的能举报
    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool admin = loggedIn && account->user().role == QStringLiteral("admin");
    const bool mine = loggedIn && account->user().id == comment.author.id;

    auto *actions = new QHBoxLayout();
    actions->setContentsMargins(0, 8, 0, 0);
    actions->setSpacing(6);
    appendActButton(actions, tr("回复"), false, [this, name]() {
        if (!ForumAccountService::instance()->isLoggedIn())
        {
            emit loginRequested();
            return;
        }
        if (!ForumAccountService::instance()->hasPassedQuiz())
        {
            openQuizDialog();
            return;
        }
        if (!m_commentEdit)
            return;
        const QString prefix = QStringLiteral("@%1 ").arg(name);
        if (!m_commentEdit->toPlainText().contains(prefix))
        {
            m_commentEdit->moveCursor(QTextCursor::End);
            m_commentEdit->insertPlainText(prefix);
        }
        scrollToComposer();
    });
    if (loggedIn && !mine)
        appendActButton(actions, tr("举报"), false,
                        [this, comment]() { reportComment(comment); });
    if (loggedIn && (mine || admin))
        appendActButton(actions, tr("删除"), true,
                        [this, comment]() { confirmDeleteComment(comment); });
    actions->addStretch(1);
    bodyLayout->addLayout(actions);

    row->addWidget(body, 1);
    return card;
}

void ForumThreadDetailPage::buildCommentComposerCard()
{
    m_commentComposerCard = new QFrame(m_mainColumn);
    m_commentComposerCard->setObjectName(QStringLiteral("forumComposer"));
    m_commentComposerBody = new QVBoxLayout(m_commentComposerCard);
    m_commentComposerBody->setContentsMargins(20, 18, 20, 18);
    m_commentComposerBody->setSpacing(12);
    m_commentComposerCard->hide();
}

void ForumThreadDetailPage::renderCommentComposer()
{
    if (!m_commentComposerBody || !m_commentComposerCard)
        return;

    // 重建前先把已写的内容收起来，重新挂上后原样放回
    if (m_commentEdit)
        m_commentDraft = m_commentEdit->toPlainText();

    ForumKit::clearLayout(m_commentComposerBody);
    m_commentEdit = nullptr;
    m_commentSend = nullptr;
    m_commentCharCount = nullptr;

    if (!isResourceThread() || !m_loaded)
    {
        m_commentComposerCard->hide();
        return;
    }

    m_commentComposerCard->show();

    auto addTitle = [this](const QString &text) {
        auto *title = new QLabel(text, m_commentComposerCard);
        title->setObjectName(QStringLiteral("forumComposerTitle"));
        title->setWordWrap(true);
        m_commentComposerBody->addWidget(title);
    };
    auto addHint = [this](const QString &text) {
        m_commentComposerBody->addWidget(ForumKit::makeMuted(m_commentComposerCard, text));
    };
    auto addPrimary = [this](const QString &text, std::function<void()> onClick) {
        auto *foot = new QHBoxLayout();
        auto *button = new QPushButton(text, m_commentComposerCard);
        button->setObjectName(QStringLiteral("forumPrimaryButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(32);
        connect(button, &QPushButton::clicked, this, [onClick]() { onClick(); });
        foot->addWidget(button);
        foot->addStretch(1);
        m_commentComposerBody->addLayout(foot);
    };

    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool quizPassed = account->hasPassedQuiz();
    const bool admin = loggedIn && account->user().role == QStringLiteral("admin");

    if (m_info.locked && !admin)
    {
        addTitle(tr("主题已锁定"));
        addHint(tr("这个主题已由管理员锁定，暂时不能评论。"));
        return;
    }

    if (!loggedIn)
    {
        addTitle(tr("来说两句？"));
        addHint(tr("登录后就能评论这个资源，也可以用同一账号在方块盒子论坛发帖。"));
        addPrimary(tr("登录 / 注册"), [this]() { emit loginRequested(); });
        return;
    }

    if (!quizPassed)
    {
        addTitle(tr("先通过入站答题，再来评论"));
        addHint(tr("答题结果记在你的账号上：随机抽 50 题、80 分通过，答错会立刻给出答案和解析。"));
        addPrimary(tr("去答题"), [this]() { openQuizDialog(); });
        return;
    }

    const ForumUser me = account->user();
    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(10);
    headRow->addWidget(ForumKit::makeAvatar(m_commentComposerCard, me.avatar, authorDisplay(me), 32));
    auto *title = new QLabel(tr("发表评论"), m_commentComposerCard);
    title->setObjectName(QStringLiteral("forumComposerTitle"));
    headRow->addWidget(title);
    headRow->addStretch(1);
    auto *headHint = new QLabel(tr("支持 Markdown · 可直接粘贴图片"), m_commentComposerCard);
    headHint->setObjectName(QStringLiteral("forumComposerHint"));
    headRow->addWidget(headHint);
    m_commentComposerBody->addLayout(headRow);

    m_commentEdit = new QPlainTextEdit(m_commentComposerCard);
    m_commentEdit->setObjectName(QStringLiteral("forumReplyEdit"));
    m_commentEdit->setPlaceholderText(
        tr("看过之后想说点什么…支持 Markdown：**加粗**、`代码`；输入 @ 可以选人，也能粘贴图片"));
    m_commentEdit->setFixedHeight(120);
    m_commentEdit->installEventFilter(this);
    connect(m_commentEdit, &QPlainTextEdit::textChanged, this, [this]() {
        m_commentDraft = m_commentEdit->toPlainText();
        if (m_commentCharCount)
            m_commentCharCount->setText(tr("%1 字").arg(m_commentDraft.trimmed().size()));
    });
    m_commentComposerBody->addWidget(m_commentEdit);

    auto *footRow = new QHBoxLayout();
    footRow->setSpacing(10);
    auto *identity =
        new QLabel(tr("以 %1 的身份评论 · Ctrl + Enter 发送").arg(authorDisplay(me)),
                   m_commentComposerCard);
    identity->setObjectName(QStringLiteral("forumComposerHint"));
    footRow->addWidget(identity);
    footRow->addStretch(1);

    m_commentCharCount = new QLabel(tr("0 字"), m_commentComposerCard);
    m_commentCharCount->setObjectName(QStringLiteral("forumComposerCount"));
    footRow->addWidget(m_commentCharCount);

    m_commentSend = new QPushButton(tr("发表评论"), m_commentComposerCard);
    m_commentSend->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_commentSend->setCursor(Qt::PointingHandCursor);
    m_commentSend->setFixedHeight(32);
    m_commentSend->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/send.svg"),
        QColor(StyleKit::resolve(QStringLiteral("@TEXT_ON_PRIMARY@"))), 14));
    m_commentSend->setIconSize(QSize(14, 14));
    connect(m_commentSend, &QPushButton::clicked, this, &ForumThreadDetailPage::submitComment);
    footRow->addWidget(m_commentSend);

    m_commentComposerBody->addLayout(footRow);

    if (!m_commentDraft.isEmpty())
    {
        m_commentEdit->setPlainText(m_commentDraft);
        m_commentEdit->moveCursor(QTextCursor::End);
    }
    if (m_commentCharCount)
        m_commentCharCount->setText(tr("%1 字").arg(m_commentDraft.trimmed().size()));
}

void ForumThreadDetailPage::submitComment()
{
    if (!m_commentEdit || !m_commentSend)
        return;

    const QString content = m_commentEdit->toPlainText().trimmed();
    if (content.isEmpty())
    {
        NotificationManager::showInfo(this, tr("评论内容不能为空"));
        return;
    }

    m_commentSend->setEnabled(false);
    const QString originText = m_commentSend->text();
    m_commentSend->setText(tr("发送中…"));

    ForumContentService::instance()->postComment(
        m_threadId, content,
        [this, originText](const ForumComment &) {
            if (m_commentSend)
            {
                m_commentSend->setEnabled(true);
                m_commentSend->setText(originText);
            }
            m_commentDraft.clear();
            if (m_commentEdit)
                m_commentEdit->clear();
            NotificationManager::showSuccess(this, tr("评论已发布"));
            // 综合排序下新评论会排在最前面，直接回第一页看自己那条
            loadComments(1);
        },
        [this, originText](const QString &message, int) {
            if (m_commentSend)
            {
                m_commentSend->setEnabled(true);
                m_commentSend->setText(originText);
            }
            NotificationManager::showError(this, message);
        });
}

void ForumThreadDetailPage::confirmDeleteComment(const ForumComment &comment)
{
    const auto reply = AppMessageBox::question(
        this, tr("删除评论"),
        tr("确定删除这条评论吗？此操作不可撤销。"),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    ForumContentService::instance()->deleteComment(
        comment.id,
        [this]() {
            NotificationManager::showSuccess(this, tr("评论已删除"));
            loadComments(m_commentPage);
        },
        [this](const QString &message, int) {
            NotificationManager::showError(this, message);
        });
}

void ForumThreadDetailPage::reportComment(const ForumComment &comment)
{
    bool ok = false;
    const QString reason = AppInputDialog::getText(
        this, tr("举报评论"),
        tr("写一句举报理由（例如：广告、人身攻击、刷屏），管理员会尽快处理"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;

    ForumContentService::instance()->reportComment(
        comment.id, QStringLiteral("other"), reason.trimmed(),
        [this]() { NotificationManager::showSuccess(this, tr("已提交举报，管理员会尽快处理")); },
        [this](const QString &message, int) {
            NotificationManager::showError(this, message);
        });
}

// ────────────────────────────────────────────────────────────── 回复框

void ForumThreadDetailPage::buildComposerCard()
{
    m_composerCard = new QFrame(m_mainColumn);
    m_composerCard->setObjectName(QStringLiteral("forumComposer"));
    m_composerBody = new QVBoxLayout(m_composerCard);
    m_composerBody->setContentsMargins(20, 18, 20, 18);
    m_composerBody->setSpacing(12);
    m_composerCard->hide();
}

void ForumThreadDetailPage::renderComposer()
{
    if (!m_composerBody)
        return;

    // 资源帖用评论框，不用楼层回复框
    if (isResourceThread())
    {
        m_composerCard->hide();
        return;
    }

    // 重建前先把已写的内容收起来，重新挂上后原样放回
    if (m_replyEdit)
        m_replyDraft = m_replyEdit->toPlainText();

    ForumKit::clearLayout(m_composerBody);
    m_replyEdit = nullptr;
    m_replySend = nullptr;
    m_replyCount = nullptr;
    m_replyTargetHost = nullptr;

    auto *account = ForumAccountService::instance();
    const bool loggedIn = account->isLoggedIn();
    const bool quizPassed = account->hasPassedQuiz();
    const bool admin = loggedIn && account->user().role == QStringLiteral("admin");
    const bool locked = m_info.locked && !admin;

    auto addTitle = [this](const QString &text) {
        auto *title = new QLabel(text, m_composerCard);
        title->setObjectName(QStringLiteral("forumComposerTitle"));
        title->setWordWrap(true);
        m_composerBody->addWidget(title);
    };
    auto addHint = [this](const QString &text) {
        auto *hint = ForumKit::makeMuted(m_composerCard, text);
        m_composerBody->addWidget(hint);
    };
    auto addPrimary = [this, account](const QString &text, std::function<void()> onClick) {
        auto *foot = new QHBoxLayout();
        auto *button = new QPushButton(text, m_composerCard);
        button->setObjectName(QStringLiteral("forumPrimaryButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(32);
        connect(button, &QPushButton::clicked, this, [onClick]() { onClick(); });
        foot->addWidget(button);
        foot->addStretch(1);
        m_composerBody->addLayout(foot);
        Q_UNUSED(account);
    };

    if (!m_loaded)
    {
        m_composerCard->hide();
        return;
    }
    m_composerCard->show();

    if (locked)
    {
        addTitle(tr("主题已锁定"));
        addHint(tr("这个主题已由管理员锁定，暂时不能回复。"));
        return;
    }

    if (!loggedIn)
    {
        addTitle(tr("想参与讨论？"));
        addHint(tr("登录后就能回复这个主题，也可以用同一账号在方块盒子论坛发帖。"));
        addPrimary(tr("登录 / 注册"), [this]() { emit loginRequested(); });
        return;
    }

    if (!quizPassed)
    {
        addTitle(tr("先通过入站答题，再来回复"));
        addHint(tr("答题结果记在你的账号上：随机抽 50 题、80 分通过，答错会立刻给出答案和解析。"));
        addPrimary(tr("去答题"), [this]() { openQuizDialog(); });
        return;
    }

    // ── 正常回复状态 ──
    auto *headRow = new QHBoxLayout();
    headRow->setSpacing(10);
    const ForumUser me = account->user();
    headRow->addWidget(ForumKit::makeAvatar(m_composerCard, me.avatar, authorDisplay(me), 32));

    auto *title = new QLabel(tr("回复主题"), m_composerCard);
    title->setObjectName(QStringLiteral("forumComposerTitle"));
    headRow->addWidget(title);
    headRow->addStretch(1);

    auto *headHint = new QLabel(tr("支持 Markdown · 可直接粘贴图片"), m_composerCard);
    headHint->setObjectName(QStringLiteral("forumComposerHint"));
    headRow->addWidget(headHint);
    m_composerBody->addLayout(headRow);

    // 回复目标条
    auto *targetHost = new QWidget(m_composerCard);
    targetHost->setAttribute(Qt::WA_StyledBackground, false);
    m_replyTargetHost = new QVBoxLayout(targetHost);
    m_replyTargetHost->setContentsMargins(0, 0, 0, 0);
    m_replyTargetHost->setSpacing(0);
    m_composerBody->addWidget(targetHost);

    m_replyEdit = new QPlainTextEdit(m_composerCard);
    m_replyEdit->setObjectName(QStringLiteral("forumReplyEdit"));
    m_replyEdit->setPlaceholderText(
        tr("说点什么…支持 Markdown：**加粗**、`代码`、> 引用；输入 @ 可以选人，也能直接粘贴或拖入图片"));
    m_replyEdit->setFixedHeight(140);
    m_replyEdit->installEventFilter(this);
    connect(m_replyEdit, &QPlainTextEdit::textChanged, this, [this]() {
        m_replyDraft = m_replyEdit->toPlainText();
        updateReplyCount();
    });
    m_composerBody->addWidget(m_replyEdit);

    auto *footRow = new QHBoxLayout();
    footRow->setSpacing(10);
    auto *identity = new QLabel(tr("以 %1 的身份回复 · Ctrl + Enter 发送")
                                    .arg(authorDisplay(me)),
                                m_composerCard);
    identity->setObjectName(QStringLiteral("forumComposerHint"));
    footRow->addWidget(identity);
    footRow->addStretch(1);

    m_replyCount = new QLabel(tr("0 字"), m_composerCard);
    m_replyCount->setObjectName(QStringLiteral("forumComposerCount"));
    footRow->addWidget(m_replyCount);

    m_replySend = new QPushButton(tr("发表回复"), m_composerCard);
    m_replySend->setObjectName(QStringLiteral("forumPrimaryButton"));
    m_replySend->setCursor(Qt::PointingHandCursor);
    m_replySend->setFixedHeight(32);
    m_replySend->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/send.svg"),
        QColor(StyleKit::resolve(QStringLiteral("@TEXT_ON_PRIMARY@"))), 14));
    m_replySend->setIconSize(QSize(14, 14));
    connect(m_replySend, &QPushButton::clicked, this, &ForumThreadDetailPage::submitReply);
    footRow->addWidget(m_replySend);

    m_composerBody->addLayout(footRow);

    if (!m_replyDraft.isEmpty())
    {
        m_replyEdit->setPlainText(m_replyDraft);
        m_replyEdit->moveCursor(QTextCursor::End);
    }
    updateReplyCount();
    renderReplyTarget();
}

void ForumThreadDetailPage::renderReplyTarget()
{
    if (!m_replyTargetHost)
        return;

    ForumKit::clearLayout(m_replyTargetHost);
    if (m_replyTargetPostId <= 0)
        return;

    auto *bar = new QFrame(m_composerCard);
    bar->setObjectName(QStringLiteral("forumReplyTargetBar"));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 8, 8, 8);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("正在回复 <b>%1</b> 的第 %2 楼")
                                 .arg(m_replyTargetName.toHtmlEscaped())
                                 .arg(m_replyTargetFloor),
                             bar);
    label->setObjectName(QStringLiteral("forumReplyTarget"));
    label->setTextFormat(Qt::RichText);
    layout->addWidget(label, 1);

    auto *clear = new QPushButton(tr("取消"), bar);
    clear->setObjectName(QStringLiteral("forumReplyTargetClear"));
    clear->setFlat(true);
    clear->setCursor(Qt::PointingHandCursor);
    clear->setFixedHeight(22);
    connect(clear, &QPushButton::clicked, this, [this]() { clearReplyTarget(); });
    layout->addWidget(clear);

    m_replyTargetHost->addWidget(bar);
}

void ForumThreadDetailPage::updateReplyCount()
{
    if (!m_replyCount || !m_replyEdit)
        return;
    m_replyCount->setText(tr("%1 字").arg(m_replyEdit->toPlainText().trimmed().size()));
}

void ForumThreadDetailPage::updateReplyBoxState()
{
    renderComposer();
    renderCommentComposer();
}

void ForumThreadDetailPage::setReplyTarget(const ForumPost &post)
{
    // 回复楼中楼时也挂到它所在的父楼层（论坛固定两层结构）
    m_replyTargetPostId = post.parentPostId > 0 ? post.parentPostId : post.id;
    m_replyTargetFloor = post.floor;
    m_replyTargetName = authorDisplay(post.author);
    renderReplyTarget();
}

void ForumThreadDetailPage::clearReplyTarget()
{
    m_replyTargetPostId = 0;
    m_replyTargetFloor = 0;
    m_replyTargetName.clear();
    renderReplyTarget();
}

void ForumThreadDetailPage::loadQuote(const ForumPost &post)
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        emit loginRequested();
        return;
    }
    if (!ForumAccountService::instance()->hasPassedQuiz())
    {
        openQuizDialog();
        return;
    }
    if (!m_replyEdit)
        renderComposer();
    if (!m_replyEdit)
        return;

    QString quoted = post.content.trimmed();
    quoted.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    if (quoted.size() > 200)
        quoted = quoted.left(200) + QStringLiteral("…");

    const QString prefix = QStringLiteral("> %1\n\n").arg(quoted);
    if (m_replyEdit->toPlainText().trimmed().isEmpty())
    {
        m_replyEdit->setPlainText(prefix);
    }
    else
    {
        m_replyEdit->moveCursor(QTextCursor::End);
        m_replyEdit->insertPlainText(QStringLiteral("\n") + prefix);
    }

    m_replyEdit->moveCursor(QTextCursor::End);
    scrollToComposer();
    m_replyEdit->setFocus();
}

void ForumThreadDetailPage::scrollToComposer()
{
    // 资源帖滚到评论框，普通帖滚到楼层回复框
    if (isResourceThread())
    {
        if (!m_commentComposerCard)
            return;
        m_scroll->ensureWidgetVisible(m_commentComposerCard, 0, 24);
        if (m_commentEdit)
            m_commentEdit->setFocus();
        return;
    }

    if (!m_composerCard)
        return;
    m_scroll->ensureWidgetVisible(m_composerCard, 0, 24);
    if (m_replyEdit)
        m_replyEdit->setFocus();
}

void ForumThreadDetailPage::submitReply()
{
    if (!m_replyEdit || !m_replySend)
        return;

    const QString content = m_replyEdit->toPlainText().trimmed();
    if (content.isEmpty())
    {
        NotificationManager::showInfo(this, tr("回复内容不能为空"));
        return;
    }

    m_replySend->setEnabled(false);
    const QString originText = m_replySend->text();
    m_replySend->setText(tr("发送中…"));

    const bool isSubReply = m_replyTargetPostId > 0;
    ForumContentService::instance()->replyToThread(
        m_threadId, content, m_replyTargetPostId,
        [this, isSubReply, originText]() {
            if (m_replySend)
            {
                m_replySend->setEnabled(true);
                m_replySend->setText(originText);
            }
            m_replyDraft.clear();
            if (m_replyEdit)
                m_replyEdit->clear();
            if (isSubReply)
                clearReplyTarget();
            NotificationManager::showSuccess(this, tr("回复已发送"));
            // 先拿到最新总页数，再翻到最后一页看自己那条
            ForumContentService::instance()->fetchThread(
                m_threadId, 1,
                [this](const ForumThreadDetail &detail) {
                    loadPage(qMax(1, detail.pagination.pages), true);
                },
                [this](const QString &, int) { loadPage(m_totalPages, true); });
        },
        [this, originText](const QString &message, int) {
            if (m_replySend)
            {
                m_replySend->setEnabled(true);
                m_replySend->setText(originText);
            }
            NotificationManager::showError(this, message);
        });
}

void ForumThreadDetailPage::openQuizDialog()
{
    QWidget *host = window();
    QuizDialog dialog(host);
    connect(&dialog, &QuizDialog::loginRequested, this, &ForumThreadDetailPage::loginRequested);
    dialog.exec();
    // 通过后 reportQuizPass 会发 userChanged 带动 renderComposer；这里再刷一次兜底
    renderComposer();
}

// ────────────────────────────────────────────────────────────── 收藏 / 置顶 / 锁定

void ForumThreadDetailPage::toggleBookmark()
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        emit loginRequested();
        return;
    }

    m_bookmarkButton->setEnabled(false);
    ForumContentService::instance()->toggleThreadBookmark(
        m_threadId,
        [this](bool bookmarked) {
            m_info.bookmarked = bookmarked;
            if (m_bookmarkButton)
            {
                m_bookmarkButton->setEnabled(true);
                m_bookmarkButton->setIcon(IconHelper::loadColoredIcon(
                    QStringLiteral(":/Images/Icons/star.svg"),
                    QColor(StyleKit::resolve(bookmarked ? QStringLiteral("@PRIMARY@")
                                                        : QStringLiteral("@TEXT_SECONDARY@"))),
                    13));
                m_bookmarkButton->setProperty("active", bookmarked);
                style()->unpolish(m_bookmarkButton);
                style()->polish(m_bookmarkButton);
            }
            NotificationManager::showSuccess(this, bookmarked ? tr("已收藏，可在个人主页查看")
                                                              : tr("已取消收藏"));
        },
        [this](const QString &message, int) {
            if (m_bookmarkButton)
                m_bookmarkButton->setEnabled(true);
            NotificationManager::showError(this, message);
        });
}

void ForumThreadDetailPage::tipThread()
{
    if (!ForumAccountService::instance()->isLoggedIn())
    {
        emit loginRequested();
        return;
    }

    // 打赏是烧自己的方块换这条帖子的推流，给自己的帖子打赏没有意义，直接拦下
    if (m_info.authorId == ForumAccountService::instance()->user().id)
    {
        NotificationManager::showInfo(this, tr("不能打赏自己的帖子"));
        return;
    }

    const int amount = ThreadTipDialog::tip(this, m_info);
    if (amount > 0)
        NotificationManager::showSuccess(this, tr("打赏 %1 方块成功，本帖推流已提升").arg(amount));
}

void ForumThreadDetailPage::togglePin()
{
    const bool target = !m_info.pinned;
    ForumContentService::instance()->setThreadPinned(
        m_threadId, target,
        [this, target]() {
            m_info.pinned = target;
            NotificationManager::showSuccess(this, target ? tr("已置顶") : tr("已取消置顶"));
            renderHero();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::toggleLock()
{
    const bool target = !m_info.locked;
    ForumContentService::instance()->setThreadLocked(
        m_threadId, target,
        [this, target]() {
            m_info.locked = target;
            NotificationManager::showSuccess(this, target ? tr("已锁定，暂不接受新回复")
                                                          : tr("已取消锁定"));
            renderHero();
            renderComposer();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

// ────────────────────────────────────────────────────────────── 复制链接

void ForumThreadDetailPage::copyThreadLink()
{
    QString base = ForumAccountService::instance()->baseUrl();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QString url = QStringLiteral("%1/thread.html?id=%2").arg(base).arg(m_threadId);
    QApplication::clipboard()->setText(url);
    NotificationManager::showSuccess(this, tr("本页链接已复制"));
}

void ForumThreadDetailPage::copyPostLink(const ForumPost &post)
{
    QString base = ForumAccountService::instance()->baseUrl();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QString url =
        QStringLiteral("%1/thread.html?id=%2#post-%3").arg(base).arg(m_threadId).arg(post.id);
    QApplication::clipboard()->setText(url);
    NotificationManager::showSuccess(this, tr("楼层链接已复制"));
}

void ForumThreadDetailPage::shareThread()
{
    QString base = ForumAccountService::instance()->baseUrl();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QString url = QStringLiteral("%1/thread.html?id=%2").arg(base).arg(m_threadId);

    auto *button = qobject_cast<QPushButton *>(sender());
    QWidget *anchor = button ? static_cast<QWidget *>(button) : this;
    QMenu menu(anchor);
    menu.addAction(tr("复制链接"), this, [this]() { copyThreadLink(); });
    menu.addAction(tr("复制标题和链接"), this, [this, url]() {
        QApplication::clipboard()->setText(
            QStringLiteral("%1\n%2").arg(m_info.title, url));
        NotificationManager::showSuccess(this, tr("标题和链接已复制，去聊天窗口粘贴给朋友吧"));
    });
    if (ForumAccountService::instance()->isLoggedIn())
    {
        menu.addAction(tr("分享给好友"), this, [this, url]() {
            ShareToFriendDialog::share(this, m_info.title, url, m_info.boardName);
        });
    }
    menu.exec(button ? button->mapToGlobal(QPoint(0, button->height() + 4))
                     : QCursor::pos());
}

// ────────────────────────────────────────────────────────────── 楼层编辑 / 删除 / 举报

void ForumThreadDetailPage::startEditPost(const ForumPost &post)
{
    const CardRef ref = m_postCards.value(post.id);
    if (ref.card.isNull() || ref.browser.isNull())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("编辑回复"));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *edit = new QPlainTextEdit(&dialog);
    edit->setPlainText(post.content);
    edit->setPlaceholderText(tr("支持 Markdown"));
    edit->setMinimumHeight(180);
    layout->addWidget(edit);
    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), &dialog);
    auto *save = new QPushButton(tr("保存"), &dialog);
    save->setObjectName(QStringLiteral("forumPrimaryButton"));
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString content = edit->toPlainText().trimmed();
    if (content.isEmpty() || content == post.content)
        return;

    ForumContentService::instance()->editPost(
        post.id, content,
        [this]() {
            NotificationManager::showSuccess(this, tr("回复已更新"));
            reload();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::confirmDeletePost(const ForumPost &post)
{
    const QString question = post.parentPostId > 0
                                 ? tr("确定删除这条楼中楼回复吗？此操作不可撤销。")
                                 : tr("确定删除这条回复吗？楼中楼会一并隐藏，此操作不可撤销。");
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("删除回复"), question, AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    ForumContentService::instance()->deletePost(
        post.id,
        [this]() {
            NotificationManager::showSuccess(this, tr("回复已删除"));
            reload();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::confirmDeleteThread()
{
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("删除主题"), tr("确定删除这个主题吗？所有楼层都不再显示，此操作不可撤销。"),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    ForumContentService::instance()->deleteThread(
        m_threadId,
        [this]() {
            NotificationManager::showSuccess(this, tr("主题已删除"));
            clearContent();
            emit threadDeleted();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::editThreadMeta()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("编辑标题与标签"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel(tr("标题"), &dialog));
    auto *titleEdit = new QLineEdit(m_info.title, &dialog);
    layout->addWidget(titleEdit);

    layout->addWidget(new QLabel(tr("标签（用逗号分隔，最多 4 个）"), &dialog));
    auto *tagsEdit = new QLineEdit(m_info.tags.join(QStringLiteral(", ")), &dialog);
    layout->addWidget(tagsEdit);

    // 插件帖 / 投影帖：顺带编辑绑定的相关链接（普通讨论帖没有这一节）
    ForumLinkRowsEditor *linksEditor = nullptr;
    const QVector<ForumResourceLink> currentLinks =
        m_info.plugin.isValid() ? m_info.plugin.links : m_info.schematic.links;
    if (m_info.plugin.isValid() || m_info.schematic.isValid())
    {
        layout->addSpacing(8);
        layout->addWidget(new QLabel(tr("相关链接（官网 / 仓库 / 反馈等，最多 6 条）"), &dialog));
        linksEditor = new ForumLinkRowsEditor(&dialog);
        linksEditor->setLinks(currentLinks);
        layout->addWidget(linksEditor);
    }

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("取消"), &dialog);
    auto *save = new QPushButton(tr("保存"), &dialog);
    save->setObjectName(QStringLiteral("forumPrimaryButton"));
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString title = titleEdit->text().trimmed();
    QStringList tags;
    for (const QString &tag : tagsEdit->text().split(QLatin1Char(',')))
    {
        const QString trimmed = tag.trimmed();
        if (!trimmed.isEmpty())
            tags.append(trimmed);
    }
    if (tags.size() > 4)
        tags = tags.mid(0, 4);
    if (title.isEmpty())
    {
        NotificationManager::showInfo(this, tr("标题不能为空"));
        return;
    }

    // 无链接编辑器的帖子也要带回原值，避免服务端把已有链接清掉
    const QVector<ForumResourceLink> links =
        linksEditor ? linksEditor->links() : currentLinks;
    ForumContentService::instance()->editThread(
        m_threadId, title, tags, links,
        [this]() {
            NotificationManager::showSuccess(this, tr("主题已更新"));
            reload();
        },
        [this](const QString &message, int) { NotificationManager::showError(this, message); });
}

void ForumThreadDetailPage::reportPost(const ForumPost &post)
{
    // 举报走独立整页：带上楼层信息，由 MainWindow 打开举报页
    emit reportRequested(post.id, post.floor, authorDisplay(post.author),
                         postExcerptOf(post.content));
}

// ────────────────────────────────────────────────────────────── 右栏

void ForumThreadDetailPage::buildRail()
{
    m_railLayout->addStretch(1);
}

void ForumThreadDetailPage::renderRail()
{
    if (!m_railLayout)
        return;

    ForumKit::clearLayout(m_railLayout);
    m_pluginInstallButton = nullptr;
    m_pluginStatusLabel = nullptr;
    m_pluginVersionsLayout = nullptr;
    m_schematicDownloadButton = nullptr;
    m_schematicStatusLabel = nullptr;
    m_railThreadHost = nullptr;
    m_railThreadList = nullptr;
    m_railThreadIds.clear();

    if (!m_info.isValid())
    {
        m_railLayout->addStretch(1);
        return;
    }

    // 插件帖的左栏就是「下载」这一列：下载卡 + 插件信息 + 相关链接 + 同版块主题
    if (m_info.plugin.isValid())
    {
        appendPluginDownloadCard();
        appendPluginInfoCard();
        if (!m_info.plugin.links.isEmpty())
            appendResourceLinksCard(m_info.plugin.links);
        appendRailThreadsCard();
        m_railLayout->addStretch(1);
        return;
    }

    // 投影帖的左栏同样以「下载」为主：下载卡 + 相关链接 + 同版块主题
    if (m_info.schematic.isValid())
    {
        appendSchematicDownloadCard();
        if (!m_info.schematic.links.isEmpty())
            appendResourceLinksCard(m_info.schematic.links);
        appendRailThreadsCard();
        m_railLayout->addStretch(1);
        return;
    }

    // ── 主题信息 ──
    auto *info = addRailCard(tr("主题信息"));

    auto *statsRow = new QHBoxLayout();
    statsRow->setSpacing(10);
    auto addStat = [statsRow](const QString &value, const QString &caption, QWidget *parent) {
        auto *box = new QFrame(parent);
        box->setObjectName(QStringLiteral("forumStatBox"));
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(12, 9, 12, 9);
        boxLayout->setSpacing(0);
        auto *valueLabel = new QLabel(value, box);
        valueLabel->setObjectName(QStringLiteral("forumStatValue"));
        boxLayout->addWidget(valueLabel);
        auto *captionLabel = new QLabel(caption, box);
        captionLabel->setObjectName(QStringLiteral("forumRailKey"));
        boxLayout->addWidget(captionLabel);
        statsRow->addWidget(box, 1);
    };
    addStat(QString::number(m_info.views), tr("浏览"), m_railHost);
    // 资源帖的讨论是评论，不是楼层回复
    const bool resourceMode = isResourceThread();
    addStat(resourceMode ? QString::number(m_commentTotal > 0 ? m_commentTotal : m_info.comments)
                         : QString::number(m_info.replies),
            resourceMode ? tr("评论") : tr("回复"), m_railHost);
    info->addLayout(statsRow);

    auto addInfoRow = [this, info](const QString &key, QWidget *value) {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        auto *keyLabel = new QLabel(key, m_railHost);
        keyLabel->setObjectName(QStringLiteral("forumRailKey"));
        keyLabel->setFixedWidth(52);
        row->addWidget(keyLabel, 0, Qt::AlignTop);
        row->addWidget(value, 1);
        info->addLayout(row);
    };

    if (!m_info.boardSlug.isEmpty())
    {
        auto *link = new QPushButton(m_info.boardName, m_railHost);
        link->setObjectName(QStringLiteral("forumRailLink"));
        link->setFlat(true);
        link->setCursor(Qt::PointingHandCursor);
        const QString slug = m_info.boardSlug;
        const QString boardName = m_info.boardName;
        connect(link, &QPushButton::clicked, this,
                [this, slug, boardName]() { emit boardOpenRequested(slug, boardName); });
        addInfoRow(tr("版块"), link);
    }

    if (m_info.authorId > 0)
    {
        auto *link = new QPushButton(m_info.authorName, m_railHost);
        link->setObjectName(QStringLiteral("forumRailLink"));
        link->setFlat(true);
        link->setCursor(Qt::PointingHandCursor);
        const int authorId = m_info.authorId;
        connect(link, &QPushButton::clicked, this,
                [this, authorId]() { emit userOpenRequested(authorId); });
        addInfoRow(tr("楼主"), link);
    }

    auto *published = new QLabel(absoluteTime(m_info.createdAt), m_railHost);
    published->setObjectName(QStringLiteral("forumRailValue"));
    published->setWordWrap(true);
    addInfoRow(tr("发布"), published);

    auto *lastReply = new QLabel(
        m_info.lastPostAt > 0 ? ForumKit::relativeTime(m_info.lastPostAt) : QStringLiteral("—"),
        m_railHost);
    lastReply->setObjectName(QStringLiteral("forumRailValue"));
    addInfoRow(resourceMode ? tr("最后活动") : tr("最后回复"), lastReply);

    if (m_info.locked)
    {
        auto *note = new QLabel(resourceMode ? tr("该主题已锁定，暂不接受新评论")
                                             : tr("该主题已锁定，暂不接受新回复"),
                                m_railHost);
        note->setObjectName(QStringLiteral("forumRailNote"));
        note->setWordWrap(true);
        info->addWidget(note);
    }

    // 刷新 / 复制并排放一行：竖向叠两个同款按钮太占位置，也显得散
    auto *actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(8);

    auto *refresh = new QPushButton(tr("刷新本页"), m_railHost);
    refresh->setObjectName(QStringLiteral("forumRailButton"));
    refresh->setCursor(Qt::PointingHandCursor);
    refresh->setFixedHeight(30);
    connect(refresh, &QPushButton::clicked, this, [this]() { reload(); });
    actionsRow->addWidget(refresh, 1);

    auto *copy = new QPushButton(tr("复制链接"), m_railHost);
    copy->setObjectName(QStringLiteral("forumRailButton"));
    copy->setCursor(Qt::PointingHandCursor);
    copy->setFixedHeight(30);
    connect(copy, &QPushButton::clicked, this, &ForumThreadDetailPage::copyThreadLink);
    actionsRow->addWidget(copy, 1);

    info->addSpacing(2);
    info->addLayout(actionsRow);

    // ── 教程帖：目录（正文标题自动生成）+ 资料附件 ──
    if (isTutorialThread())
    {
        appendTutorialTocCard();
        appendTutorialAttachmentsCard();
    }

    // ── 同版块主题 ──
    appendRailThreadsCard();

    // ── 社区提示 ──
    auto *tips = addRailCard(tr("社区提示"));
    const QStringList tipList{tr("友善交流，就事论事"), tr("提问前先搜一下同类问题"),
                              tr("图片可直接粘贴进回复框")};
    for (const QString &tip : tipList)
    {
        auto *label = new QLabel(QStringLiteral("<span style=\"color:%1;font-weight:800;\">✓</span>"
                                                "<span style=\"color:%2;\">　%3</span>")
                                     .arg(StyleKit::resolve(QStringLiteral("@PRIMARY@")),
                                          StyleKit::resolve(QStringLiteral("@TEXT_SECONDARY@")),
                                          tip.toHtmlEscaped()),
                                 m_railHost);
        label->setObjectName(QStringLiteral("forumRailTip"));
        label->setWordWrap(true);
        tips->addWidget(label);
    }

    m_railLayout->addStretch(1);
}

/** 教程目录卡：正文一 / 二 / 三级标题自动生成，点击滚到对应章节 */
void ForumThreadDetailPage::appendTutorialTocCard()
{
    if (!m_bodyBrowser)
        return;
    const QVector<ForumTocEntry> entries = m_bodyBrowser->toc();
    if (entries.isEmpty())
        return;   // 正文还没渲染好，或者作者没写标题：不摆空卡

    auto *card = addRailCard(tr("目录"));
    for (const ForumTocEntry &entry : entries)
    {
        auto *button = new QPushButton(entry.text, m_railHost);
        button->setObjectName(QStringLiteral("forumRailLink"));
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tr("跳到这一节"));
        // 二 / 三级标题往里缩进，与网页版目录的层级观感一致
        button->setStyleSheet(
            QStringLiteral("text-align:left; padding-left:%1px; font-weight:%2;")
                .arg(2 + (entry.level - 1) * 14)
                .arg(entry.level == 1 ? QStringLiteral("700") : QStringLiteral("400")));
        const QString anchor = entry.anchor;
        connect(button, &QPushButton::clicked, this, [this, anchor]() {
            if (!m_bodyBrowser || !m_content)
                return;
            const int y = m_bodyBrowser->anchorY(anchor);
            if (y < 0)
                return;
            // 浏览器本身没有内部滚动条：算出锚点在整页滚动内容里的 y 再滚过去
            const int absolute = m_bodyBrowser->mapTo(m_content, QPoint(0, 0)).y() + y;
            m_scroll->verticalScrollBar()->setValue(qMax(0, absolute - 16));
        });
        card->addWidget(button);
    }
}

/** 教程资料附件卡：pdf / 压缩包等，点一下取地址保存到本地 */
void ForumThreadDetailPage::appendTutorialAttachmentsCard()
{
    if (m_info.attachments.isEmpty())
        return;

    auto *card = addRailCard(tr("资料附件"));
    for (const ForumAttachmentInfo &attachment : m_info.attachments)
    {
        auto *button = new QPushButton(
            QStringLiteral("📎 %1 · %2").arg(
                attachment.name.isEmpty() ? attachment.fileName : attachment.name,
                ForumKit::humanFileSize(attachment.fileSize)),
            m_railHost);
        button->setObjectName(QStringLiteral("forumRailLink"));
        button->setFlat(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tr("下载这个附件（%1 次下载）").arg(attachment.downloads));
        button->setStyleSheet(QStringLiteral("text-align:left;"));
        connect(button, &QPushButton::clicked, this, [this, attachment, button]() {
            const int attachmentId = attachment.id;
            button->setEnabled(false);
            auto *service = ForumContentService::instance();
            service->fetchAttachmentDownload(
                m_threadId, attachmentId,
                [this, button](const ForumAttachmentDownload &download) {
                    if (button)
                        button->setEnabled(true);
                    if (!download.isValid())
                    {
                        setStatus(tr("拿不到附件下载地址，稍后再试"), true);
                        return;
                    }
                    const QString target = QFileDialog::getSaveFileName(
                        window(), tr("保存资料附件"),
                        download.filename.isEmpty() ? QStringLiteral("attachment.bin")
                                                    : download.filename);
                    if (target.isEmpty())
                        return;

                    auto *manager = new QNetworkAccessManager(this);
                    QNetworkRequest request{QUrl(download.url)};
                    request.setHeader(QNetworkRequest::UserAgentHeader,
                                      QStringLiteral("BlockBoxLauncher"));
                    request.setTransferTimeout(300000);
                    auto *reply = manager->get(request);
                    connect(reply, &QNetworkReply::finished, this,
                            [this, reply, manager, target]() {
                                reply->deleteLater();
                                manager->deleteLater();
                                const QByteArray data = reply->readAll();
                                if (reply->error() != QNetworkReply::NoError || data.isEmpty())
                                {
                                    setStatus(tr("附件下载失败，请检查网络后再试"), true);
                                    return;
                                }
                                QFile file(target);
                                if (!file.open(QIODevice::WriteOnly)
                                    || file.write(data) != data.size())
                                {
                                    setStatus(tr("附件写不进所选位置"), true);
                                    return;
                                }
                                file.close();
                                setStatus(tr("附件已保存"));
                            });
                },
                [this, button](const QString &message, int) {
                    if (button)
                        button->setEnabled(true);
                    setStatus(message, true);
                });
        });
        card->addWidget(button);
    }
}

QVBoxLayout *ForumThreadDetailPage::addRailCard(const QString &title)
{    auto *card = new QFrame(m_railHost);
    card->setObjectName(QStringLiteral("forumRailCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(12);

    // 标题前一根主色小竖条：和发布页、大厅卡片同一套「区块标题」语言
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    auto *bar = new QFrame(card);
    bar->setObjectName(QStringLiteral("forumRailBar"));
    bar->setFixedSize(3, 14);
    titleRow->addWidget(bar, 0, Qt::AlignVCenter);
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("forumRailTitle"));
    titleRow->addWidget(titleLabel);
    titleRow->addStretch(1);
    layout->addLayout(titleRow);

    m_railLayout->addWidget(card);
    return layout;
}

void ForumThreadDetailPage::appendRailThreadsCard()
{
    auto *board = addRailCard(tr("同版块主题"));

    auto *listHost = new QWidget(m_railHost);
    listHost->setAttribute(Qt::WA_StyledBackground, false);
    auto *listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    auto *loading = new QLabel(tr("加载中…"), listHost);
    loading->setObjectName(QStringLiteral("forumRailEmpty"));
    listLayout->addWidget(loading);
    board->addWidget(listHost);

    m_railThreadList = listLayout;
    m_railThreadHost = listHost;
    if (!m_info.boardSlug.isEmpty())
        loadRailThreads(m_info.boardSlug);
}

void ForumThreadDetailPage::loadRailThreads(const QString &slug)
{
    ForumContentService::instance()->fetchBoardThreads(
        slug, QStringLiteral("latest"), 1,
        [this](const ForumBoard &, const QVector<ForumThreadInfo> &threads,
               const ForumPagination &) {
            if (!m_railThreadList)
                return;
            ForumKit::clearLayout(m_railThreadList);
            m_railThreadIds.clear();

            QVector<ForumThreadInfo> others;
            for (const ForumThreadInfo &thread : threads)
            {
                if (thread.id == m_threadId)
                    continue;
                others.append(thread);
                if (others.size() >= 5)
                    break;
            }

            if (others.isEmpty())
            {
                auto *empty = new QLabel(tr("这个版块还没有其他主题"), m_railThreadHost);
                empty->setObjectName(QStringLiteral("forumRailEmpty"));
                m_railThreadList->addWidget(empty);
                return;
            }

            for (const ForumThreadInfo &thread : others)
            {
                // 用 QFrame 而不是 QPushButton：#forumRailThread 里放的是「标题 + 元信息」两行，
                // QPushButton 的 sizeHint 不吃子布局，会被压成一行高、文字被裁掉。
                auto *item = new QFrame(m_railThreadHost);
                item->setObjectName(QStringLiteral("forumRailThread"));
                item->setProperty("first", m_railThreadIds.isEmpty());
                item->setCursor(Qt::PointingHandCursor);
                item->setToolTip(thread.title);
                auto *itemLayout = new QVBoxLayout(item);
                itemLayout->setContentsMargins(2, 8, 2, 9);
                itemLayout->setSpacing(2);
                auto *titleLabel = ForumKit::makeElided(item, thread.title);
                titleLabel->setObjectName(QStringLiteral("forumRailThreadTitle"));
                itemLayout->addWidget(titleLabel);
                auto *meta = new QLabel(tr("%1 · %2 回复").arg(thread.authorName).arg(thread.replies),
                                        item);
                meta->setObjectName(QStringLiteral("forumRailThreadMeta"));
                itemLayout->addWidget(meta);
                m_railThreadIds.insert(item, thread.id);
                item->installEventFilter(this);
                m_railThreadList->addWidget(item);
            }
        },
        [this](const QString &, int) {
            if (!m_railThreadList)
                return;
            ForumKit::clearLayout(m_railThreadList);
            auto *empty = new QLabel(tr("暂时读不到同版块主题"), m_railThreadHost);
            empty->setObjectName(QStringLiteral("forumRailEmpty"));
            m_railThreadList->addWidget(empty);
        });
}

// ────────────────────────────────────────────────────────────── 加载

void ForumThreadDetailPage::openThread(int threadId)
{
    if (m_threadId == threadId && m_loaded)
        return;
    clearContent();
    m_threadId = threadId;
    loadPage(1);
}

void ForumThreadDetailPage::clearContent()
{
    m_threadId = 0;
    m_info = ForumThreadInfo();
    m_page = 1;
    m_totalPages = 1;
    m_loaded = false;
    m_postCards.clear();
    m_editingPostId = 0;
    m_replyDraft.clear();
    m_commentDraft.clear();
    m_commentPage = 1;
    m_commentPages = 1;
    m_commentTotal = 0;
    m_pluginBusy = false;
    m_pluginUploadingVersion = false;
    m_pluginInstallButton = nullptr;
    m_pluginStatusLabel = nullptr;
    m_pluginVersionsLayout = nullptr;
    m_bodyMarkdown.clear();
    clearReplyTarget();

    renderHero();
    applyLayoutMode();
    renderBodyCard();
    ForumKit::clearLayout(m_floorsLayout);
    m_pager->setPage(1, 1);
    renderComposer();
    renderCommentComposer();
    setStatus(QString());

    if (m_railLayout)
    {
        ForumKit::clearLayout(m_railLayout);
        m_railThreadHost = nullptr;
        m_railThreadList = nullptr;
        m_railThreadIds.clear();
        m_railLayout->addStretch(1);
    }
}

void ForumThreadDetailPage::loadPage(int page, bool scrollToBottomAfter)
{
    if (m_threadId <= 0)
        return;

    setStatus(tr("正在加载帖子…"));

    ForumContentService::instance()->fetchThread(
        m_threadId, page,
        [this, scrollToBottomAfter](const ForumThreadDetail &detail) {
            if (m_threadId != detail.info.id)
                return; // 已经切到别的帖子了，丢弃过期结果

            m_info = detail.info;
            m_page = qMax(1, detail.pagination.page);
            m_totalPages = qMax(1, detail.pagination.pages);
            m_loaded = true;
            setStatus(QString());

            renderHero();
            if (isResourceThread())
            {
                // 资源帖：作者写的介绍正文（1 楼）先摆出来，下面才是评论。
                // 翻评论页时楼层接口翻过了 1 楼就不再带它，所以只在看到时更新缓存。
                for (const ForumPost &post : detail.posts)
                {
                    if (post.floor == 1 && !post.deleted && !post.content.isEmpty())
                    {
                        m_bodyMarkdown = post.content;
                        break;
                    }
                }
                renderBodyCard();
                loadComments(m_page);
            }
            else
            {
                // 教程帖：1 楼正文单独渲染成「教程正文」卡（右栏目录靠它生成跳转锚点），
                // 楼层列表从 2 楼开始，回复体系与普通帖完全一样
                QVector<ForumPost> floors = detail.posts;
                if (isTutorialThread())
                {
                    for (const ForumPost &post : detail.posts)
                    {
                        if (post.floor == 1 && !post.deleted && !post.content.isEmpty())
                        {
                            m_bodyMarkdown = post.content;
                            break;
                        }
                    }
                    floors.removeIf([](const ForumPost &post) { return post.floor == 1; });
                }
                renderBodyCard();
                renderPosts(floors);
                m_pager->setPage(m_page, m_totalPages);
            }
            renderComposer();
            renderCommentComposer();
            renderRail();
            applyLayoutMode();

            updateCommentHeader();

            QMetaObject::invokeMethod(this, [this, scrollToBottomAfter]() {
                if (scrollToBottomAfter)
                    m_scroll->verticalScrollBar()->setValue(
                        m_scroll->verticalScrollBar()->maximum());
                else
                    m_scroll->verticalScrollBar()->setValue(0);
            }, Qt::QueuedConnection);
        },
        [this](const QString &message, int httpStatus) {
            Q_UNUSED(httpStatus);
            setStatus(tr("加载失败：%1").arg(message), true);
        });
}

void ForumThreadDetailPage::reload(bool scrollToBottomAfter)
{
    if (m_threadId > 0)
        loadPage(m_page, scrollToBottomAfter);
}

// ────────────────────────────────────────────────────────────── 杂项

bool ForumThreadDetailPage::eventFilter(QObject *watched, QEvent *event)
{
    if (m_scroll && watched == m_scroll->viewport() && event->type() == QEvent::Resize)
        applyPageWidth();

    if (watched == m_replyEdit && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->modifiers() & Qt::ControlModifier) && (key->key() == Qt::Key_Return
                                                         || key->key() == Qt::Key_Enter))
        {
            submitReply();
            return true;
        }
    }
    if (watched == m_commentEdit && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->modifiers() & Qt::ControlModifier) && (key->key() == Qt::Key_Return
                                                         || key->key() == Qt::Key_Enter))
        {
            submitComment();
            return true;
        }
    }
    // 侧栏「同版块主题」整行可点（QFrame 没有 clicked 信号，走事件过滤）
    if (event->type() == QEvent::MouseButtonRelease && m_railThreadIds.contains(static_cast<QWidget *>(watched)))
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            const int threadId = m_railThreadIds.value(static_cast<QWidget *>(watched));
            if (threadId > 0)
                emit threadOpenRequested(threadId);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ForumThreadDetailPage::handleContentLink(const QUrl &url)
{
    auto *service = ForumAccountService::instance();
    QString text = url.toString();
    const QString base = service->baseUrl();
    // 论坛站内链接：绝对地址先去掉站点前缀，统一按站内路径处理
    if (!base.isEmpty() && text.startsWith(base))
        text = text.mid(base.size());

    if (text.contains(QStringLiteral("thread.html")))
    {
        bool ok = false;
        const int id = QUrlQuery(QUrl(text)).queryItemValue(QStringLiteral("id")).toInt(&ok);
        if (ok && id > 0)
        {
            if (id == m_threadId)
                m_scroll->verticalScrollBar()->setValue(0);
            else
                emit threadOpenRequested(id); // 帖子内链：交给上层切换，返回键仍回到来源页
            return;
        }
    }
    if (text.contains(QStringLiteral("board.html")))
    {
        const QString slug = QUrlQuery(QUrl(text)).queryItemValue(QStringLiteral("slug"));
        if (!slug.isEmpty())
        {
            emit boardOpenRequested(slug, slug);
            return;
        }
    }
    // 其余都是站外链接，交给系统浏览器
    QDesktopServices::openUrl(url);
}

void ForumThreadDetailPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
    m_statusLabel->setStyleSheet(error ? StyleKit::resolve(QStringLiteral("color: @WARNING_TEXT@;"))
                                       : QString());
}
