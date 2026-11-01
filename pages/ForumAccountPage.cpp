/**
 * @file   ForumAccountPage.cpp
 * @brief  账户管理页（实验性·论坛账户版）实现
 * @author BlockBox Team
 * @date   2026-10-31
 */
#include "ForumAccountPage.h"

#include <algorithm>

#include <QDateTime>
#include <QDesktopServices>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "components/AppMessageBox.h"
#include "components/ForumKit.h"
#include "components/NotificationManager.h"
#include "layouts/MasonryLayout.h"
#include "utils/ForumAccountService.h"
#include "utils/FavoritesManager.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/SkinDownloader.h"
#include "utils/ThemeManager.h"

namespace
{

constexpr int kAvatarSize = 76;
/** 资料卡顶部封面横幅高度（宽随卡片伸缩，图片按比例铺满裁切） */
constexpr int kCoverHeight = 132;
/** 角色卡片封面统一高度（卡片等高，瀑布流按等宽等高排布） */
constexpr int kCharacterCoverHeight = 190;

/** 单行文本按控件宽度省略（论坛帖子列表的长标题） */
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) { setWordWrap(false); }

    void setFullText(const QString &text)
    {
        m_full = text;
        updateElidedText();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElidedText();
    }

private:
    void updateElidedText()
    {
        if (m_full.isEmpty())
        {
            QLabel::setText(QString());
            return;
        }
        const int available = qMax(60, width() - 2);
        QLabel::setText(fontMetrics().elidedText(m_full, Qt::ElideRight, available));
    }

    QString m_full;
};

/** 可点击的行（帖子 / 回复 / 收藏） */
class ClickableFrame : public QFrame
{
public:
    explicit ClickableFrame(QWidget *parent = nullptr) : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;
    std::function<void()> onDoubleClick;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        // 只有真的按在这一行上，后面的松开才算点击。
        // 列表重新排版/滚动时，Qt 会把松开事件补发到光标下新出现的行上，
        // 这种没有按下事件配合的松开不能当成点击（否则点标签会直接跳进帖子）。
        m_pressed = event->button() == Qt::LeftButton && rect().contains(event->pos());
        QFrame::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool pressed = m_pressed;
        m_pressed = false;
        if (pressed && event->button() == Qt::LeftButton && rect().contains(event->pos()) && onClick)
            onClick();
        QFrame::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        m_pressed = false;
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && onDoubleClick)
            onDoubleClick();
        QFrame::mouseDoubleClickEvent(event);
    }

private:
    bool m_pressed = false;
};

/** 只按当前页算尺寸的 QStackedWidget：避免「账号设置」这种长页面把别的页撑高 */
class AdaptiveStack : public QStackedWidget
{
public:
    explicit AdaptiveStack(QWidget *parent = nullptr) : QStackedWidget(parent)
    {
        connect(this, &QStackedWidget::currentChanged, this, [this](int) { updateGeometry(); });
    }

    QSize sizeHint() const override
    {
        if (QWidget *page = currentWidget())
            return page->sizeHint();
        return QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        if (QWidget *page = currentWidget())
            return page->minimumSizeHint();
        return QStackedWidget::minimumSizeHint();
    }
};

/** 圆形裁切头像 */
QPixmap roundPixmap(const QPixmap &source, int size)
{
    if (source.isNull() || size <= 0)
        return QPixmap();

    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(clip);

    const QPixmap scaled =
        source.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    painter.drawPixmap((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    return canvas;
}

/** 首字母头像（论坛默认头像同款：绿底白字） */
QPixmap letterAvatar(const QString &name, int size, const QColor &background)
{
    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(11, size / 2));
    painter.setFont(font);
    painter.setPen(Qt::white);

    const QString trimmed = name.trimmed();
    const QString letter = trimmed.isEmpty() ? QStringLiteral("?") : trimmed.left(1).toUpper();
    painter.drawText(canvas.rect(), Qt::AlignCenter, letter);
    return canvas;
}

/** 未登录头像：绿底 + 玩家剪影（纯手绘，不依赖图标资源） */
QPixmap guestAvatar(int size, const QColor &background)
{
    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);

    QPainterPath circle;
    circle.addEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));
    painter.drawPath(circle);

    painter.setClipPath(circle);
    const QColor silhouette(255, 255, 255, 235);
    painter.setBrush(silhouette);

    // 头
    const qreal headRadius = size * 0.135;
    painter.drawEllipse(QPointF(size / 2.0, size * 0.40), headRadius, headRadius);

    // 肩
    QPainterPath shoulders;
    shoulders.addRoundedRect(QRectF(size * 0.26, size * 0.56, size * 0.48, size * 0.40),
                             size * 0.24, size * 0.24);
    painter.drawPath(shoulders);

    return canvas;
}

/** 论坛网页同款相对时间 */
QString relativeTime(qint64 seconds)
{
    if (seconds <= 0)
        return QString();

    const qint64 diff = QDateTime::currentSecsSinceEpoch() - seconds;
    if (diff < 60)
        return QObject::tr("刚刚");
    if (diff < 3600)
        return QObject::tr("%1 分钟前").arg(diff / 60);
    if (diff < 86400)
        return QObject::tr("%1 小时前").arg(diff / 3600);
    if (diff < 86400 * 30)
        return QObject::tr("%1 天前").arg(diff / 86400);
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd"));
}

/** 通知列表的日期分组标题：今天 / 昨天 / 11月1日 */
QString notificationDayGroup(qint64 seconds)
{
    const QDateTime when = QDateTime::fromSecsSinceEpoch(seconds);
    if (!when.isValid() || seconds <= 0)
        return QObject::tr("更早");

    const QDate day = when.date();
    const QDate today = QDate::currentDate();
    if (day == today)
        return QObject::tr("今天");
    if (day == today.addDays(-1))
        return QObject::tr("昨天");
    if (day.year() == today.year())
        return day.toString(QObject::tr("M月d日"));
    return day.toString(QStringLiteral("yyyy-MM-dd"));
}

/** 表单字段：标签在上，控件在下（对照论坛网页 .field） */
QWidget *buildField(const QString &labelText, QWidget *editor, QWidget *parent)
{
    auto *field = new QWidget(parent);
    auto *layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *label = new QLabel(labelText, field);
    label->setObjectName(QStringLiteral("fbFieldLabel"));
    layout->addWidget(label);
    layout->addWidget(editor);
    return field;
}

QLabel *makeMutedLabel(const QString &text, QWidget *parent, const char *objectName)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setWordWrap(true);
    return label;
}

QFrame *makeDivider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("fbDivider"));
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    return line;
}

/**
 * 把一张皮肤纹理拼成 16×32 的正面角色图（像素风）：
 * 头 / 身体 / 双臂 / 双腿，modern（64×64）皮肤连同帽子、外套、袖子等覆盖层一起叠上去。
 */
QImage composeFrontModel(const QImage &skin)
{
    if (skin.isNull() || skin.width() < 64)
        return QImage();

    const bool modern = skin.height() >= 64;
    QImage model(16, 32, QImage::Format_ARGB32_Premultiplied);
    model.fill(Qt::transparent);

    QPainter painter(&model);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    auto blit = [&](int sx, int sy, int sw, int sh, int dx, int dy, bool mirror = false) {
        QImage part = skin.copy(sx, sy, sw, sh);
        if (part.isNull())
            return;
        if (mirror)
            part = part.flipped(Qt::Horizontal);
        painter.drawImage(QRect(dx, dy, sw, sh), part);
    };

    // 头（+ 帽子 / 头发层）
    blit(8, 8, 8, 8, 4, 0);
    if (modern)
        blit(40, 8, 8, 8, 4, 0);

    // 身体（+ 外套层）
    blit(20, 20, 8, 12, 4, 8);
    if (modern)
        blit(20, 36, 8, 12, 4, 8);

    // 右臂 / 左臂
    blit(44, 20, 4, 12, 0, 8);
    if (modern)
    {
        blit(44, 36, 4, 12, 0, 8);
        blit(36, 52, 4, 12, 12, 8);
        blit(52, 52, 4, 12, 12, 8);
    }
    else
    {
        blit(44, 20, 4, 12, 12, 8, true);
    }

    // 右腿 / 左腿
    blit(4, 20, 4, 12, 4, 20);
    if (modern)
    {
        blit(4, 36, 4, 12, 4, 20);
        blit(20, 52, 4, 12, 8, 20);
        blit(4, 52, 4, 12, 8, 20);
    }
    else
    {
        blit(4, 20, 4, 12, 8, 20, true);
    }

    return model;
}

/** 账户类型短文案（离线 / 微软正版 / 第三方服务器名） */
QString accountTypeText(const AccountInfo &account)
{
    const QString type = account.type;
    if (type.contains(QStringLiteral("微软")) || type.contains(QStringLiteral("Microsoft"),
                                                             Qt::CaseInsensitive))
        return QObject::tr("微软正版");
    if (type.contains(QObject::tr("离线")))
        return QObject::tr("离线");
    return type.isEmpty() ? QObject::tr("第三方") : type;
}

} // namespace

// ════════════════════════════════════════════════════════════════ 生命周期

ForumAccountPage::ForumAccountPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("fbRoot"));
    m_service = ForumAccountService::instance();
    m_network = new QNetworkAccessManager(this);

    buildUi();
    applyTheme();
    renderProfile();
    renderContentArea();
    updateFooterNote();

    // 论坛账号状态
    connect(m_service, &ForumAccountService::userChanged, this, [this](const ForumUser &user) {
        Q_UNUSED(user);
        m_busy = false;
        if (m_authSubmitBtn)
        {
            m_authSubmitBtn->setEnabled(true);
            setAuthMode(m_authMode);
        }
        if (m_service->isLoggedIn())
            showOverview();
        renderProfile();
        renderContentArea();
        if (m_service->isLoggedIn())
        {
            m_threadsLoaded = m_repliesLoaded = m_bookmarksLoaded = false;
            m_contentFavorites.clear();
            m_contentFavoritesLoaded = false;
            m_contentFavoriteTotal = 0;
            refreshBookmarkStat();
            loadTabData();
        }
    });

    connect(m_service, &ForumAccountService::sessionExpired, this, [this]() {
        m_threads = QJsonArray();
        m_replies = QJsonArray();
        m_bookmarks = QJsonArray();
        m_threadsLoaded = m_repliesLoaded = m_bookmarksLoaded = false;
        m_threadTotal = m_replyTotal = m_bookmarkTotal = 0;
        m_contentFavorites.clear();
        m_contentFavoritesLoaded = false;
        m_contentFavoriteTotal = 0;
        showOverview();
        renderProfile();
        renderContentArea();
        NotificationManager::showInfo(this, tr("论坛登录状态已失效，请重新登录"));
    });

    connect(m_service, &ForumAccountService::requestFailed, this,
            [this](const QString &message, int status) {
                m_busy = false;
                if (m_authSubmitBtn)
                {
                    m_authSubmitBtn->setEnabled(true);
                    setAuthMode(m_authMode);
                }
                if (status != 0 && m_viewStack && m_viewStack->currentIndex() == OverviewView)
                    NotificationManager::showError(this, message);
                else if (m_authError && m_viewStack && m_viewStack->currentIndex() == AuthView)
                {
                    m_authError->setText(message);
                    m_authError->show();
                }
                else
                    NotificationManager::showError(this, message);
            });

    connect(m_service, &ForumAccountService::requestSucceeded, this, [this](const QString &message) {
        m_busy = false;
        if (m_authError)
            m_authError->clear();
        NotificationManager::showSuccess(this, message);
    });

    // 论坛地址 / 主题变化
    connect(SettingsManager::instance(), &SettingsManager::forumBaseUrlChanged, this,
            [this](const QString &) {
                updateFooterNote();
                if (m_service->hasSession())
                    refreshAccount();
            });

    if (ThemeManager::instance())
    {
        connect(ThemeManager::instance(), &ThemeManager::themeChanged, this,
                [this](ThemeManager::ThemeType) { applyTheme(); });
        connect(ThemeManager::instance(), &ThemeManager::themeColorChanged, this,
                [this](const QString &) { applyTheme(); });
    }
}

ForumAccountPage::~ForumAccountPage() = default;

QString ForumAccountPage::currentAccountName() const
{
    return m_service->isLoggedIn() ? m_service->user().displayName : tr("未登录");
}

void ForumAccountPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 每次进入页面都重新同步一次（论坛上的主题数、昵称可能已经变了）
    QTimer::singleShot(0, this, [this]() { refreshAccount(); });
}

bool ForumAccountPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_detailSkinWidget && event->type() == QEvent::Resize)
        fitDetailCamera();
    return QWidget::eventFilter(watched, event);
}

void ForumAccountPage::fitDetailCamera()
{
    if (!m_detailSkinWidget || m_detailSkinWidget->height() <= 0)
        return;

    // 与首页皮肤面板同一套算法：fovY 40°，模型高 2.0、臂展 1.0（1 像素 = 1/16 单位）
    const qreal aspect = qreal(m_detailSkinWidget->width()) / qreal(m_detailSkinWidget->height());
    constexpr float kFovTan = 0.36397f;   // tan(20°)
    constexpr float kMargin = 1.10f;      // 模型外扩余量（与经典账户页观感一致）
    const float fitHeight = (2.0f * kMargin) / (2.0f * kFovTan);
    const float fitWidth = (1.0f * kMargin / float(aspect > 0 ? aspect : 1.0)) / (2.0f * kFovTan);
    m_detailSkinWidget->resetView(qMax(fitHeight, fitWidth) * 1.50f);
    // 稍微压低俯视角，正面感更强（默认 28.6° 俯视略陡）
    m_detailSkinWidget->rotateView(0.0f, -0.15f);
}

// ════════════════════════════════════════════════════════════════ 构建界面

void ForumAccountPage::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("fbScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:none;}"
                                           "QScrollArea > QWidget > QWidget{background:transparent;}"));
    rootLayout->addWidget(m_scroll);

    auto *content = new QWidget(m_scroll);
    content->setObjectName(QStringLiteral("fbContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 14, 24, 20);
    contentLayout->setSpacing(12);

    m_viewStack = new AdaptiveStack(content);
    m_viewStack->setObjectName(QStringLiteral("fbViewStack"));
    m_viewStack->addWidget(buildOverview(m_viewStack));
    m_viewStack->addWidget(buildAuthCard(m_viewStack));
    m_viewStack->addWidget(buildAccountDetailView(m_viewStack));
    // 卡片按内容高度排布（和论坛网页一致），窗口更高时留白在下方，不把卡片拉长
    contentLayout->addWidget(m_viewStack, 0);
    contentLayout->addStretch(1);

    // ── 页脚：论坛地址 + 快捷入口 ──
    auto *footer = new QFrame(content);
    footer->setObjectName(QStringLiteral("fbFooter"));
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(14, 10, 12, 10);
    footerLayout->setSpacing(8);

    m_footerNote = new QLabel(footer);
    m_footerNote->setObjectName(QStringLiteral("fbNote"));
    m_footerNote->setWordWrap(true);
    m_footerNote->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_footerNote->setOpenExternalLinks(true);
    footerLayout->addWidget(m_footerNote, 1);

    // 论坛主页 / 论坛设置（原来在页面顶栏，那条栏去掉后挪到这里）
    auto *openForumBtn = new QPushButton(tr("打开论坛"), footer);
    openForumBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    openForumBtn->setFixedHeight(30);
    openForumBtn->setCursor(Qt::PointingHandCursor);
    openForumBtn->setToolTip(tr("在浏览器里打开方块盒子论坛"));
    connect(openForumBtn, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(m_service->baseUrl()));
    });
    footerLayout->addWidget(openForumBtn);

    auto *forumSettingsBtn = new QPushButton(tr("论坛设置"), footer);
    forumSettingsBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    forumSettingsBtn->setFixedHeight(30);
    forumSettingsBtn->setCursor(Qt::PointingHandCursor);
    forumSettingsBtn->setToolTip(tr("去 设置 → 高级设置 修改论坛地址"));
    connect(forumSettingsBtn, &QPushButton::clicked, this,
            [this]() { emit openAdvancedSettingsRequested(); });
    footerLayout->addWidget(forumSettingsBtn);

    // 一键退回经典账户管理页（论坛账号之外的游戏账户仍在那边管理）
    auto *classicBtn = new QPushButton(tr("切换回经典界面"), footer);
    classicBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    classicBtn->setFixedHeight(30);
    classicBtn->setCursor(Qt::PointingHandCursor);
    classicBtn->setToolTip(tr("回到原来的账户管理页（管理 Minecraft 账户）"));
    connect(classicBtn, &QPushButton::clicked, this, []() {
        if (SettingsManager::instance())
            SettingsManager::instance()->setExperimentalForumAccountEnabled(false);
    });
    footerLayout->addWidget(classicBtn);

    contentLayout->addWidget(footer);

    m_scroll->setWidget(content);
}

QWidget *ForumAccountPage::buildOverview(QWidget *parent)
{
    auto *overview = new QWidget(parent);
    auto *layout = new QVBoxLayout(overview);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    layout->addWidget(buildProfileCard(overview));
    layout->addWidget(buildTabsCard(overview), 0);

    return overview;
}

QWidget *ForumAccountPage::buildProfileCard(QWidget *parent)
{
    m_profileCard = new QFrame(parent);
    m_profileCard->setObjectName(QStringLiteral("fbProfileCard"));
    auto *outer = new QVBoxLayout(m_profileCard);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 封面横幅：贴在卡片顶部，顶部两角跟着卡片圆角一起裁
    m_coverBanner = new ForumKit::CoverBanner(m_profileCard);
    m_coverBanner->setFixedHeight(kCoverHeight);
    m_coverBanner->setRadius(16);
    outer->addWidget(m_coverBanner);

    auto *body = new QWidget(m_profileCard);
    auto *layout = new QHBoxLayout(body);
    layout->setContentsMargins(22, 18, 22, 20);
    layout->setSpacing(18);

    // 头像
    m_avatarLabel = new QLabel(body);
    m_avatarLabel->setFixedSize(kAvatarSize, kAvatarSize);
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_avatarLabel, 0, Qt::AlignTop);

    // 资料
    auto *infoColumn = new QVBoxLayout();
    infoColumn->setContentsMargins(0, 0, 0, 0);
    infoColumn->setSpacing(2);

    auto *nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(8);
    m_nameLabel = new QLabel(tr("未登录"), body);
    m_nameLabel->setObjectName(QStringLiteral("fbName"));
    m_nameLabel->setWordWrap(true);
    nameRow->addWidget(m_nameLabel);

    m_roleChip = new QLabel(body);
    m_roleChip->setObjectName(QStringLiteral("fbChip"));
    m_roleChip->hide();
    nameRow->addWidget(m_roleChip);
    nameRow->addStretch();
    infoColumn->addLayout(nameRow);

    m_usernameLabel = new QLabel(body);
    m_usernameLabel->setObjectName(QStringLiteral("fbUsername"));
    m_usernameLabel->setWordWrap(true);
    infoColumn->addWidget(m_usernameLabel);

    m_bioLabel = new QLabel(body);
    m_bioLabel->setObjectName(QStringLiteral("fbBio"));
    m_bioLabel->setWordWrap(true);
    m_bioLabel->hide();
    infoColumn->addWidget(m_bioLabel);

    infoColumn->addSpacing(10);

    // 数据行：主题 / 回复 / 收藏 / 加入 / 游戏账户
    auto *statRow = new QHBoxLayout();
    statRow->setContentsMargins(0, 0, 0, 0);
    statRow->setSpacing(22);

    const QStringList statLabels = {tr("主题"), tr("回复"), tr("收藏"), tr("加入"), tr("游戏账户")};
    QList<QLabel **> statTargets = {&m_statThreads, &m_statPosts, &m_statBookmarks, &m_statJoined,
                                    &m_statGameAccounts};
    for (int i = 0; i < statLabels.size(); ++i)
    {
        auto *statWidget = new QWidget(body);
        auto *statLayout = new QVBoxLayout(statWidget);
        statLayout->setContentsMargins(0, 0, 0, 0);
        statLayout->setSpacing(0);

        auto *value = new QLabel(QStringLiteral("0"), statWidget);
        value->setObjectName(QStringLiteral("fbStatValue"));
        auto *caption = new QLabel(statLabels.at(i), statWidget);
        caption->setObjectName(QStringLiteral("fbStatLabel"));
        statLayout->addWidget(value);
        statLayout->addWidget(caption);

        *statTargets.at(i) = value;
        statRow->addWidget(statWidget);
    }
    statRow->addStretch();
    infoColumn->addLayout(statRow);

    layout->addLayout(infoColumn, 1);

    // 右侧操作
    auto *actionColumn = new QVBoxLayout();
    actionColumn->setContentsMargins(0, 0, 0, 0);
    actionColumn->setSpacing(8);
    actionColumn->setAlignment(Qt::AlignTop);

    m_profileAuthActions = new QWidget(body);
    auto *authActionsLayout = new QHBoxLayout(m_profileAuthActions);
    authActionsLayout->setContentsMargins(0, 0, 0, 0);
    authActionsLayout->setSpacing(8);

    auto *loginBtn = new QPushButton(tr("登录"), m_profileAuthActions);
    loginBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    loginBtn->setFixedHeight(36);
    loginBtn->setMinimumWidth(78);
    loginBtn->setCursor(Qt::PointingHandCursor);
    connect(loginBtn, &QPushButton::clicked, this, [this]() { openAuth(LoginMode); });
    authActionsLayout->addWidget(loginBtn);

    auto *registerBtn = new QPushButton(tr("注册"), m_profileAuthActions);
    registerBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
    registerBtn->setFixedHeight(36);
    registerBtn->setMinimumWidth(78);
    registerBtn->setCursor(Qt::PointingHandCursor);
    connect(registerBtn, &QPushButton::clicked, this, [this]() { openAuth(RegisterMode); });
    authActionsLayout->addWidget(registerBtn);

    actionColumn->addWidget(m_profileAuthActions, 0, Qt::AlignRight);

    m_profileUserActions = new QWidget(body);
    auto *userActionsLayout = new QHBoxLayout(m_profileUserActions);
    userActionsLayout->setContentsMargins(0, 0, 0, 0);
    userActionsLayout->setSpacing(8);

    auto *editBtn = new QPushButton(tr("编辑资料"), m_profileUserActions);
    editBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    editBtn->setFixedHeight(36);
    editBtn->setMinimumWidth(78);
    editBtn->setCursor(Qt::PointingHandCursor);
    connect(editBtn, &QPushButton::clicked, this, [this]() { setActiveTab(SettingsTab); });
    userActionsLayout->addWidget(editBtn);

    auto *logoutBtn = new QPushButton(tr("退出登录"), m_profileUserActions);
    logoutBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    logoutBtn->setFixedHeight(36);
    logoutBtn->setCursor(Qt::PointingHandCursor);
    connect(logoutBtn, &QPushButton::clicked, this, &ForumAccountPage::confirmLogout);
    userActionsLayout->addWidget(logoutBtn);

    m_profileUserActions->hide();
    actionColumn->addWidget(m_profileUserActions, 0, Qt::AlignRight);

    layout->addLayout(actionColumn);

    outer->addWidget(body);

    return m_profileCard;
}

QWidget *ForumAccountPage::buildTabsCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("fbCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 工具条：分段页签 + 提示 + 刷新
    auto *toolbar = new QWidget(card);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(14, 12, 14, 12);
    toolbarLayout->setSpacing(10);

    auto *tabsTrack = new QWidget(toolbar);
    tabsTrack->setObjectName(QStringLiteral("fbTabsTrack"));
    auto *tabsLayout = new QHBoxLayout(tabsTrack);
    tabsLayout->setContentsMargins(4, 4, 4, 4);
    tabsLayout->setSpacing(4);

    const QStringList tabLabels = {tr("角色"), tr("我的主题"), tr("我的回复"), tr("我的收藏"),
                                   tr("通知"), tr("账号设置")};
    for (int i = 0; i < tabLabels.size(); ++i)
    {
        auto *tab = new QPushButton(tabLabels.at(i), tabsTrack);
        tab->setObjectName(QStringLiteral("fbTab"));
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setChecked(i == 0);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFixedHeight(30);
        connect(tab, &QPushButton::clicked, this, [this, i]() { setActiveTab(TabIndex(i)); });
        tabsLayout->addWidget(tab);
        m_tabButtons.append(tab);
    }
    toolbarLayout->addWidget(tabsTrack);

    m_tabHint = new QLabel(toolbar);
    m_tabHint->setObjectName(QStringLiteral("fbNote"));
    toolbarLayout->addWidget(m_tabHint);
    toolbarLayout->addStretch();

    m_refreshBtn = new QPushButton(tr("刷新"), toolbar);
    m_refreshBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    m_refreshBtn->setFixedHeight(30);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &ForumAccountPage::refreshAccount);
    toolbarLayout->addWidget(m_refreshBtn);

    layout->addWidget(toolbar);
    layout->addWidget(makeDivider(card));

    // 内容页：手工换页，避免 QStackedWidget 用最长页面把卡片撑高
    m_contentHost = new QWidget(card);
    m_contentHost->setObjectName(QStringLiteral("fbContentHost"));
    m_contentLayout = new QVBoxLayout(m_contentHost);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);

    m_contentPages.append(buildCharactersContent(m_contentHost));                   // CharactersContent
    m_contentPages.append(buildListContent(m_contentHost, &m_threadListLayout));    // ThreadsContent
    m_contentPages.append(buildListContent(m_contentHost, &m_replyListLayout));     // RepliesContent
    m_contentPages.append(buildBookmarksContent(m_contentHost));                    // BookmarksContent
    m_contentPages.append(buildNotificationsContent(m_contentHost));                // NotificationsContent
    m_contentPages.append(buildSettingsContent(m_contentHost));                     // SettingsContent
    m_contentPages.append(buildLoggedOutContent(m_contentHost));                    // LoggedOutContent
    for (QWidget *page : m_contentPages)
        page->hide();

    layout->addWidget(m_contentHost, 0);

    return card;
}

QWidget *ForumAccountPage::buildListContent(QWidget *parent, QVBoxLayout **outLayout)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 12);
    layout->setSpacing(2);
    if (outLayout)
        *outLayout = layout;
    return page;
}

QWidget *ForumAccountPage::buildBookmarksContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 12);
    layout->setSpacing(2);

    // 子页切换：资源收藏（在线资源同步）/ 帖子收藏（论坛主题）
    auto *subTrack = new QWidget(page);
    subTrack->setObjectName(QStringLiteral("fbTabsTrack"));
    auto *subLayout = new QHBoxLayout(subTrack);
    subLayout->setContentsMargins(0, 0, 0, 6);
    subLayout->setSpacing(4);
    subLayout->addStretch();  // 让切换胶囊靠右，与上方页签条错开层次

    const QStringList subLabels = {tr("资源收藏"), tr("帖子收藏")};
    for (int i = 0; i < subLabels.size(); ++i)
    {
        auto *sub = new QPushButton(subLabels.at(i), subTrack);
        sub->setObjectName(QStringLiteral("fbTab"));
        sub->setCheckable(true);
        sub->setAutoExclusive(true);
        sub->setChecked(i == m_bookmarkSubTab);
        sub->setCursor(Qt::PointingHandCursor);
        sub->setFixedHeight(28);
        connect(sub, &QPushButton::clicked, this, [this, i]() { setBookmarkSubTab(i); });
        subLayout->addWidget(sub);
        m_bookmarkSubButtons.append(sub);
    }
    layout->addWidget(subTrack, 0, Qt::AlignRight);

    auto *listHost = new QWidget(page);
    m_bookmarkListLayout = new QVBoxLayout(listHost);
    m_bookmarkListLayout->setContentsMargins(0, 0, 0, 0);
    m_bookmarkListLayout->setSpacing(2);
    layout->addWidget(listHost);

    return page;
}

QWidget *ForumAccountPage::buildCharactersContent(QWidget *parent)
{
    m_charactersPage = new QWidget(parent);
    auto *layout = new QVBoxLayout(m_charactersPage);
    layout->setContentsMargins(14, 14, 14, 16);
    layout->setSpacing(10);

    // 瀑布流布局直接装在容器控件上（与内容列表页的用法一致）
    auto *container = new QWidget(m_charactersPage);
    container->setObjectName(QStringLiteral("fbCharContainer"));
    m_charactersLayout = new MasonryLayout(container, 0, 12, 12);
    layout->addWidget(container);
    layout->addStretch(1);

    return m_charactersPage;
}

QWidget *ForumAccountPage::buildLoggedOutContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    page->setMinimumHeight(220);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 24, 20, 24);
    layout->setSpacing(8);

    layout->addStretch();

    auto *title = new QLabel(tr("还没有登录方块盒子论坛"), page);
    title->setObjectName(QStringLiteral("fbSectionTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    m_loggedOutHint = makeMutedLabel(
        tr("登录后，你在论坛发布的主题、回复和收藏都会显示在这里，也能直接在启动器里改资料、换头像。"),
        page, "fbEmpty");
    m_loggedOutHint->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_loggedOutHint);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 8, 0, 0);
    buttonRow->setSpacing(10);
    buttonRow->addStretch();

    m_loggedOutLoginBtn = new QPushButton(tr("登录"), page);
    m_loggedOutLoginBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    m_loggedOutLoginBtn->setFixedHeight(36);
    m_loggedOutLoginBtn->setMinimumWidth(96);
    m_loggedOutLoginBtn->setCursor(Qt::PointingHandCursor);
    connect(m_loggedOutLoginBtn, &QPushButton::clicked, this, [this]() { openAuth(LoginMode); });
    buttonRow->addWidget(m_loggedOutLoginBtn);

    m_loggedOutRegisterBtn = new QPushButton(tr("注册新账号"), page);
    m_loggedOutRegisterBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
    m_loggedOutRegisterBtn->setFixedHeight(36);
    m_loggedOutRegisterBtn->setCursor(Qt::PointingHandCursor);
    connect(m_loggedOutRegisterBtn, &QPushButton::clicked, this, [this]() { openAuth(RegisterMode); });
    buttonRow->addWidget(m_loggedOutRegisterBtn);

    buttonRow->addStretch();
    layout->addLayout(buttonRow);
    layout->addStretch();

    return page;
}

QWidget *ForumAccountPage::buildSettingsContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 14, 18, 18);
    layout->setSpacing(0);

    // ── 主页封面 ──
    auto *coverRow = new QHBoxLayout();
    coverRow->setContentsMargins(0, 0, 0, 0);
    coverRow->setSpacing(12);

    m_settingsCover = new ForumKit::CoverBanner(page);
    m_settingsCover->setFixedSize(136, 46);
    m_settingsCover->setRadius(8);
    coverRow->addWidget(m_settingsCover, 0, Qt::AlignVCenter);

    auto *coverColumn = new QVBoxLayout();
    coverColumn->setContentsMargins(0, 0, 0, 0);
    coverColumn->setSpacing(6);

    auto *coverHint = new QLabel(tr("主页封面：建议用 3:1 的横图，会显示在个人主页顶部；超过 3 MB 会自动压缩"), page);
    coverHint->setObjectName(QStringLiteral("fbNote"));
    coverHint->setWordWrap(true);
    coverColumn->addWidget(coverHint);

    auto *coverButtons = new QHBoxLayout();
    coverButtons->setContentsMargins(0, 0, 0, 0);
    coverButtons->setSpacing(8);

    auto *uploadCoverBtn = new QPushButton(tr("上传封面"), page);
    uploadCoverBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    uploadCoverBtn->setFixedHeight(28);
    uploadCoverBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    uploadCoverBtn->setCursor(Qt::PointingHandCursor);
    connect(uploadCoverBtn, &QPushButton::clicked, this, &ForumAccountPage::pickCoverFile);
    coverButtons->addWidget(uploadCoverBtn);

    auto *removeCoverBtn = new QPushButton(tr("删除封面"), page);
    removeCoverBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    removeCoverBtn->setFixedHeight(28);
    removeCoverBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    removeCoverBtn->setCursor(Qt::PointingHandCursor);
    connect(removeCoverBtn, &QPushButton::clicked, this, [this]() { m_service->removeCover(); });
    coverButtons->addWidget(removeCoverBtn);
    coverButtons->addStretch();

    coverColumn->addLayout(coverButtons);
    coverRow->addLayout(coverColumn, 1);

    layout->addLayout(coverRow);
    layout->addSpacing(14);

    // ── 头像 ──
    auto *avatarRow = new QHBoxLayout();
    avatarRow->setContentsMargins(0, 0, 0, 0);
    avatarRow->setSpacing(12);

    m_settingsAvatar = new QLabel(page);
    m_settingsAvatar->setFixedSize(48, 48);
    m_settingsAvatar->setAlignment(Qt::AlignCenter);
    avatarRow->addWidget(m_settingsAvatar, 0, Qt::AlignVCenter);

    auto *avatarButtons = new QVBoxLayout();
    avatarButtons->setContentsMargins(0, 0, 0, 0);
    avatarButtons->setSpacing(4);

    auto *uploadBtn = new QPushButton(tr("上传新头像"), page);
    uploadBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    uploadBtn->setFixedHeight(28);
    uploadBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    uploadBtn->setCursor(Qt::PointingHandCursor);
    connect(uploadBtn, &QPushButton::clicked, this, &ForumAccountPage::pickAvatarFile);
    avatarButtons->addWidget(uploadBtn);

    auto *removeAvatarBtn = new QPushButton(tr("删除头像"), page);
    removeAvatarBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    removeAvatarBtn->setFixedHeight(28);
    removeAvatarBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    removeAvatarBtn->setCursor(Qt::PointingHandCursor);
    connect(removeAvatarBtn, &QPushButton::clicked, this,
            [this]() { m_service->removeAvatar(); });
    avatarButtons->addWidget(removeAvatarBtn);

    avatarRow->addLayout(avatarButtons);
    avatarRow->addStretch();
    layout->addLayout(avatarRow);

    auto *avatarHint = new QLabel(tr("建议用正方形图片，超过 512 KB 会自动压缩后再上传"), page);
    avatarHint->setObjectName(QStringLiteral("fbNote"));
    avatarHint->setWordWrap(true);
    layout->addWidget(avatarHint);
    layout->addSpacing(14);

    // ── 资料 ──
    auto *profileTitle = new QLabel(tr("账号资料"), page);
    profileTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    layout->addWidget(profileTitle);
    layout->addSpacing(10);

    m_displayNameEdit = new QLineEdit(page);
    m_displayNameEdit->setObjectName(QStringLiteral("fbInput"));
    m_displayNameEdit->setPlaceholderText(tr("别人看到的名字，最多 24 个字"));
    m_displayNameEdit->setMaxLength(24);
    layout->addWidget(buildField(tr("昵称"), m_displayNameEdit, page));
    layout->addSpacing(12);

    m_emailEdit = new QLineEdit(page);
    m_emailEdit->setObjectName(QStringLiteral("fbInput"));
    m_emailEdit->setPlaceholderText(tr("选填，方便找回账号"));
    layout->addWidget(buildField(tr("邮箱"), m_emailEdit, page));
    layout->addSpacing(12);

    m_bioEdit = new QPlainTextEdit(page);
    m_bioEdit->setObjectName(QStringLiteral("fbInput"));
    m_bioEdit->setPlaceholderText(tr("介绍一下自己，最多 300 个字"));
    m_bioEdit->setFixedHeight(88);
    layout->addWidget(buildField(tr("个人简介"), m_bioEdit, page));
    layout->addSpacing(14);

    auto *saveProfileBtn = new QPushButton(tr("保存资料"), page);
    saveProfileBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
    saveProfileBtn->setFixedHeight(36);
    saveProfileBtn->setMinimumWidth(108);
    saveProfileBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    saveProfileBtn->setCursor(Qt::PointingHandCursor);
    connect(saveProfileBtn, &QPushButton::clicked, this, &ForumAccountPage::saveProfileForm);

    auto *saveProfileRow = new QHBoxLayout();
    saveProfileRow->setContentsMargins(0, 0, 0, 0);
    saveProfileRow->addWidget(saveProfileBtn);
    saveProfileRow->addStretch();
    layout->addLayout(saveProfileRow);

    layout->addSpacing(18);
    layout->addWidget(makeDivider(page));
    layout->addSpacing(16);

    // ── 密码 ──
    auto *passwordTitle = new QLabel(tr("修改密码"), page);
    passwordTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    layout->addWidget(passwordTitle);
    layout->addSpacing(10);

    m_currentPasswordEdit = new QLineEdit(page);
    m_currentPasswordEdit->setObjectName(QStringLiteral("fbInput"));
    m_currentPasswordEdit->setEchoMode(QLineEdit::Password);
    m_currentPasswordEdit->setPlaceholderText(tr("输入当前密码"));
    layout->addWidget(buildField(tr("当前密码"), m_currentPasswordEdit, page));
    layout->addSpacing(12);

    m_newPasswordEdit = new QLineEdit(page);
    m_newPasswordEdit->setObjectName(QStringLiteral("fbInput"));
    m_newPasswordEdit->setEchoMode(QLineEdit::Password);
    m_newPasswordEdit->setPlaceholderText(tr("至少 6 位"));
    layout->addWidget(buildField(tr("新密码"), m_newPasswordEdit, page));
    layout->addSpacing(12);

    m_confirmPasswordEdit = new QLineEdit(page);
    m_confirmPasswordEdit->setObjectName(QStringLiteral("fbInput"));
    m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    m_confirmPasswordEdit->setPlaceholderText(tr("再输一次新密码"));
    layout->addWidget(buildField(tr("确认新密码"), m_confirmPasswordEdit, page));
    layout->addSpacing(14);

    auto *savePasswordBtn = new QPushButton(tr("修改密码"), page);
    savePasswordBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
    savePasswordBtn->setFixedHeight(36);
    savePasswordBtn->setMinimumWidth(108);
    savePasswordBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    savePasswordBtn->setCursor(Qt::PointingHandCursor);
    connect(savePasswordBtn, &QPushButton::clicked, this, &ForumAccountPage::savePasswordForm);

    auto *savePasswordRow = new QHBoxLayout();
    savePasswordRow->setContentsMargins(0, 0, 0, 0);
    savePasswordRow->addWidget(savePasswordBtn);
    savePasswordRow->addStretch();
    layout->addLayout(savePasswordRow);

    layout->addSpacing(18);
    layout->addWidget(makeDivider(page));
    layout->addSpacing(16);

    // ── 会话 ──
    auto *sessionTitle = new QLabel(tr("登录状态"), page);
    sessionTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    layout->addWidget(sessionTitle);
    layout->addSpacing(8);

    m_settingsAccountLine = new QLabel(page);
    m_settingsAccountLine->setObjectName(QStringLiteral("fbNote"));
    m_settingsAccountLine->setWordWrap(true);
    layout->addWidget(m_settingsAccountLine);
    layout->addSpacing(12);

    auto *logoutBtn = new QPushButton(tr("退出登录"), page);
    logoutBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    logoutBtn->setFixedHeight(36);
    logoutBtn->setMinimumWidth(108);
    logoutBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    logoutBtn->setCursor(Qt::PointingHandCursor);
    connect(logoutBtn, &QPushButton::clicked, this, &ForumAccountPage::confirmLogout);

    auto *logoutRow = new QHBoxLayout();
    logoutRow->setContentsMargins(0, 0, 0, 0);
    logoutRow->addWidget(logoutBtn);
    logoutRow->addStretch();
    layout->addLayout(logoutRow);

    layout->addSpacing(18);

    return page;
}

QWidget *ForumAccountPage::buildAuthCard(QWidget *parent)
{
    auto *view = new QWidget(parent);
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    outer->addStretch();

    auto *card = new QFrame(view);
    card->setObjectName(QStringLiteral("fbAuthCard"));
    card->setFixedWidth(430);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(26, 24, 26, 22);
    layout->setSpacing(0);

    m_authTitle = new QLabel(tr("登录方块盒子论坛"), card);
    m_authTitle->setObjectName(QStringLiteral("fbAuthTitle"));
    m_authTitle->setWordWrap(true);
    layout->addWidget(m_authTitle);

    layout->addSpacing(4);
    m_authSub = new QLabel(tr("用你注册的账号继续参与讨论。"), card);
    m_authSub->setObjectName(QStringLiteral("fbAuthSub"));
    m_authSub->setWordWrap(true);
    layout->addWidget(m_authSub);
    layout->addSpacing(18);

    // 登录 / 注册 切换
    auto *tabsTrack = new QWidget(card);
    tabsTrack->setObjectName(QStringLiteral("fbAuthTabs"));
    auto *tabsLayout = new QHBoxLayout(tabsTrack);
    tabsLayout->setContentsMargins(4, 4, 4, 4);
    tabsLayout->setSpacing(4);

    const QStringList authLabels = {tr("登录"), tr("注册新账号")};
    for (int i = 0; i < authLabels.size(); ++i)
    {
        auto *tab = new QPushButton(authLabels.at(i), tabsTrack);
        tab->setObjectName(QStringLiteral("fbAuthTab"));
        tab->setCheckable(true);
        tab->setAutoExclusive(true);
        tab->setChecked(i == 0);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFixedHeight(34);
        connect(tab, &QPushButton::clicked, this, [this, i]() { setAuthMode(AuthMode(i)); });
        tabsLayout->addWidget(tab, 1);
        m_authTabButtons.append(tab);
    }
    layout->addWidget(tabsTrack);
    layout->addSpacing(16);

    m_authError = new QLabel(card);
    m_authError->setObjectName(QStringLiteral("fbError"));
    m_authError->setWordWrap(true);
    m_authError->hide();
    layout->addWidget(m_authError);
    layout->addSpacing(12);

    m_authNameEdit = new QLineEdit(card);
    m_authNameEdit->setObjectName(QStringLiteral("fbInput"));
    m_authNameEdit->setPlaceholderText(tr("中文、字母、数字，2–20 位"));
    m_authNameEdit->setMaxLength(20);
    layout->addWidget(buildField(tr("用户名"), m_authNameEdit, card));
    layout->addSpacing(14);

    m_authPasswordEdit = new QLineEdit(card);
    m_authPasswordEdit->setObjectName(QStringLiteral("fbInput"));
    m_authPasswordEdit->setEchoMode(QLineEdit::Password);
    m_authPasswordEdit->setPlaceholderText(tr("至少 6 位"));
    layout->addWidget(buildField(tr("密码"), m_authPasswordEdit, card));
    layout->addSpacing(14);

    m_confirmField = new QWidget(card);
    auto *confirmLayout = new QVBoxLayout(m_confirmField);
    confirmLayout->setContentsMargins(0, 0, 0, 0);
    confirmLayout->setSpacing(0);
    m_authConfirmEdit = new QLineEdit(m_confirmField);
    m_authConfirmEdit->setObjectName(QStringLiteral("fbInput"));
    m_authConfirmEdit->setEchoMode(QLineEdit::Password);
    m_authConfirmEdit->setPlaceholderText(tr("再输一次"));
    confirmLayout->addWidget(buildField(tr("确认密码"), m_authConfirmEdit, m_confirmField));
    m_confirmField->hide();
    layout->addWidget(m_confirmField);
    layout->addSpacing(18);

    m_authSubmitBtn = new QPushButton(tr("登录"), card);
    m_authSubmitBtn->setObjectName(QStringLiteral("fbPrimaryBlock"));
    m_authSubmitBtn->setFixedHeight(44);
    m_authSubmitBtn->setCursor(Qt::PointingHandCursor);
    connect(m_authSubmitBtn, &QPushButton::clicked, this, &ForumAccountPage::submitAuth);
    layout->addWidget(m_authSubmitBtn);

    layout->addSpacing(16);
    auto *note = makeMutedLabel(tr("注册即表示你愿意守点规矩：友善提问、不刷屏、不发广告。"), card,
                                "fbNote");
    note->setAlignment(Qt::AlignCenter);
    layout->addWidget(note);

    layout->addSpacing(10);
    auto *backBtn = new QPushButton(tr("← 返回账户管理"), card);
    backBtn->setObjectName(QStringLiteral("fbLink"));
    backBtn->setCursor(Qt::PointingHandCursor);
    connect(backBtn, &QPushButton::clicked, this, &ForumAccountPage::onBackToAccountList);

    auto *backRow = new QHBoxLayout();
    backRow->setContentsMargins(0, 0, 0, 0);
    backRow->addStretch();
    backRow->addWidget(backBtn);
    backRow->addStretch();
    layout->addLayout(backRow);

    auto *cardRow = new QHBoxLayout();
    cardRow->setContentsMargins(0, 0, 0, 0);
    cardRow->addStretch();
    cardRow->addWidget(card);
    cardRow->addStretch();
    outer->addLayout(cardRow);
    outer->addStretch();

    return view;
}

// ════════════════════════════════════════════════════════════════ 主题

ForumAccountPage::Palette ForumAccountPage::currentPalette() const
{
    Palette p;
    p.dark = ThemeManager::instance()
             && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;

    if (p.dark)
    {
        p.bg = QStringLiteral("#0e1411");
        p.bgTint = QStringLiteral("#131b16");
        p.card = QStringLiteral("#151d18");
        p.line = QStringLiteral("#24312a");
        p.line2 = QStringLiteral("#2f4038");
        p.ink = QStringLiteral("#e7f0e9");
        p.ink2 = QStringLiteral("#a7baac");
        p.ink3 = QStringLiteral("#7d9184");
        p.brand = QStringLiteral("#66bb6a");
        p.brandHover = QStringLiteral("#7cc47f");
        p.brandSoft = QStringLiteral("rgba(102, 187, 106, 14%)");
        p.brandLine = QStringLiteral("rgba(102, 187, 106, 34%)");
        p.onBrand = QStringLiteral("#08210b");
        p.danger = QStringLiteral("#ef7c74");
        p.dangerSoft = QStringLiteral("rgba(198, 40, 40, 16%)");
        p.warnSoft = QStringLiteral("rgba(178, 106, 0, 16%)");
    }
    else
    {
        p.bg = QStringLiteral("#f6faf6");
        p.bgTint = QStringLiteral("#eef6ed");
        p.card = QStringLiteral("#ffffff");
        p.line = QStringLiteral("#e4ece3");
        p.line2 = QStringLiteral("#d3e3d2");
        p.ink = QStringLiteral("#17251a");
        p.ink2 = QStringLiteral("#52665a");
        p.ink3 = QStringLiteral("#87998b");
        p.brand = QStringLiteral("#2e7d32");
        p.brandHover = QStringLiteral("#388e3c");
        p.brandSoft = QStringLiteral("rgba(46, 125, 50, 10%)");
        p.brandLine = QStringLiteral("rgba(46, 125, 50, 24%)");
        p.onBrand = QStringLiteral("#ffffff");
        p.danger = QStringLiteral("#c62828");
        p.dangerSoft = QStringLiteral("#fdecea");
        p.warnSoft = QStringLiteral("#fff5e6");
    }
    return p;
}

void ForumAccountPage::applyTheme()
{
    const Palette p = currentPalette();

    QString qss = QStringLiteral(R"QSS(
QWidget#fbRoot, QWidget#fbContent, QWidget#fbViewStack { background: transparent; }
QWidget#fbContentStack { background: transparent; }

QPushButton#fbBtnGhost {
    background: %CARD%;
    color: %INK2%;
    border: 1px solid %LINE2%;
    border-radius: 17px;
    padding: 0 14px;
    font-size: 13px;
    font-weight: 700;
}
QPushButton#fbBtnGhost:hover { border-color: %BRAND_LINE%; color: %BRAND%; }
QPushButton#fbBtnGhost:disabled { background: transparent; color: %INK3%; border-color: %LINE%; }
QPushButton#fbBtnPrimary {
    background: %BRAND%;
    color: %ON_BRAND%;
    border: 1px solid transparent;
    border-radius: 17px;
    padding: 0 16px;
    font-size: 13px;
    font-weight: 700;
}
QPushButton#fbBtnPrimary:hover { background: %BRAND_HOVER%; }
QPushButton#fbBtnPrimary:disabled { background: %LINE2%; color: %INK3%; }
QPushButton#fbDangerGhost {
    background: transparent;
    color: %INK2%;
    border: 1px solid %LINE2%;
    border-radius: 17px;
    padding: 0 14px;
    font-size: 13px;
    font-weight: 700;
}
QPushButton#fbDangerGhost:hover { background: %DANGER_SOFT%; color: %DANGER%; border-color: transparent; }

QFrame#fbCard, QFrame#fbProfileCard, QFrame#fbFooter {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 16px;
}
QFrame#fbAuthCard {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 16px;
}
QFrame#fbDivider { background: %LINE%; border: none; }

QFrame#fbCharCard {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 14px;
}
QFrame#fbCharCard:hover { border-color: %BRAND_LINE%; }
QFrame#fbCharCard[inUse="true"] { border-color: %BRAND%; }
QLabel#fbCharName { color: %INK%; font-size: 14px; font-weight: 800; }
QFrame#fbCharEmpty {
    background: %CARD%;
    border: 1px dashed %LINE2%;
    border-radius: 14px;
}

/* ── 账户详情页 ── */
QFrame#fbDetailPreview { background: transparent; border: none; }
QFrame#fbDetailPanel { background: %CARD%; border: 1px solid %LINE%; border-radius: 16px; }
QLabel#fbDetailName { color: %INK%; font-size: 19px; font-weight: 800; }
QLabel#fbDetailMeta { color: %INK3%; font-size: 12px; }
QPushButton#fbPoseButton {
    background: transparent;
    color: %INK2%;
    border: 1px solid %LINE2%;
    border-radius: 10px;
    padding: 0 8px;
    font-size: 12.5px;
    font-weight: 600;
}
QPushButton#fbPoseButton:hover { border-color: %BRAND_LINE%; color: %BRAND%; }
QPushButton#fbPoseButton:checked {
    background: %BRAND_SOFT%;
    border-color: %BRAND%;
    color: %BRAND%;
}
QPushButton#fbDetailUse {
    background: %BRAND%;
    color: %ON_BRAND%;
    border: 1px solid transparent;
    border-radius: 19px;
    font-size: 13.5px;
    font-weight: 700;
}
QPushButton#fbDetailUse:hover { background: %BRAND_HOVER%; }
QPushButton#fbDetailUse:disabled { background: %LINE2%; color: %INK3%; }
QComboBox#fbDetailCombo {
    background: %CARD%;
    color: %INK%;
    border: 1px solid %LINE2%;
    border-radius: 10px;
    padding: 5px 10px;
    font-size: 12.5px;
}
QComboBox#fbDetailCombo QAbstractItemView {
    background: %CARD%;
    color: %INK%;
    border: 1px solid %LINE2%;
    selection-background-color: %BRAND_SOFT%;
    selection-color: %BRAND%;
}

QLabel#fbName { color: %INK%; font-size: 21px; font-weight: 800; }
QLabel#fbUsername { color: %INK3%; font-size: 13px; }
QLabel#fbBio { color: %INK2%; font-size: 13.5px; }
QLabel#fbStatValue { color: %INK%; font-size: 15px; font-weight: 800; }
QLabel#fbStatLabel { color: %INK3%; font-size: 12px; }
QLabel#fbSectionTitle { color: %INK%; font-size: 15px; font-weight: 800; }
QLabel#fbChip {
    background: %BRAND_SOFT%;
    color: %BRAND%;
    border-radius: 11px;
    padding: 2px 10px;
    font-size: 12px;
    font-weight: 700;
}
QLabel#fbNote { color: %INK3%; font-size: 12.5px; }
QLabel#fbEmpty { color: %INK3%; font-size: 13.5px; }
QLabel#fbError {
    background: %DANGER_SOFT%;
    color: %DANGER%;
    border-radius: 10px;
    padding: 8px 12px;
    font-size: 12.5px;
}

QWidget#fbTabsTrack, QWidget#fbAuthTabs {
    background: %BG_TINT%;
    border-radius: 15px;
}
QPushButton#fbTab {
    background: transparent;
    color: %INK3%;
    border: 1px solid transparent;
    border-radius: 12px;
    padding: 0 14px;
    font-size: 13px;
    font-weight: 700;
}
QPushButton#fbTab:hover { color: %INK2%; }
QPushButton#fbTab:checked { background: %CARD%; color: %BRAND%; border-color: %LINE2%; }
QPushButton#fbAuthTab {
    background: transparent;
    color: %INK3%;
    border: 1px solid transparent;
    border-radius: 10px;
    font-size: 13.5px;
    font-weight: 700;
}
QPushButton#fbAuthTab:checked { background: %CARD%; color: %BRAND%; border-color: %LINE2%; }

QFrame#fbListRow {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 10px;
}
QFrame#fbListRow:hover { background: %BG_TINT%; border-color: %LINE%; }
QLabel#fbRowTitle { color: %INK%; font-size: 14px; font-weight: 600; }
QLabel#fbRowMeta { color: %INK3%; font-size: 12px; }
QLabel#fbRowChip {
    background: %BRAND_SOFT%;
    color: %BRAND%;
    border-radius: 10px;
    padding: 1px 8px;
    font-size: 11.5px;
    font-weight: 700;
}

/* ── 通知页 ── */
QLabel#fbNotifUnreadChip {
    background: %BRAND_SOFT%;
    color: %BRAND%;
    border-radius: 10px;
    padding: 3px 10px;
    font-size: 12px;
    font-weight: 800;
}
QLabel#fbNotifGroup {
    color: %INK3%;
    font-size: 12px;
    font-weight: 800;
    padding: 2px 0 0 2px;
}
QFrame#fbNotifRow {
    background: %CARD%;
    border: 1px solid %LINE%;
    border-radius: 12px;
}
QFrame#fbNotifRow:hover {
    background: %BG_TINT%;
    border-color: %BRAND_LINE%;
}
QFrame#fbNotifRow[unread="true"] {
    background: %BRAND_SOFT%;
    border-color: %BRAND_LINE%;
}
QFrame#fbNotifRow[unread="true"]:hover { border-color: %BRAND%; }
QFrame#fbNotifAccent {
    background: %BRAND%;
    border: none;
    border-radius: 2px;
}
QLabel#fbNotifIcon {
    background: %BG_TINT%;
    border: 1px solid transparent;
    border-radius: 12px;
}
QLabel#fbNotifIcon[type="mention"] { background: %BRAND_SOFT%; }
QLabel#fbNotifIcon[type="system"] { background: %WARN_SOFT%; }
QLabel#fbNotifDot {
    background: %BRAND%;
    border-radius: 3px;
}
QLabel#fbNotifTitle { color: %INK%; font-size: 14px; font-weight: 700; }
QLabel#fbNotifSub { color: %INK2%; font-size: 12.5px; }
QFrame#fbNotifQuote {
    background: %QUOTE_BG%;
    border: none;
    border-radius: 8px;
}
QFrame#fbNotifRow[unread="true"] QFrame#fbNotifQuote { background: %QUOTE_BG_ALT%; }
QLabel#fbNotifQuoteText { color: %INK2%; font-size: 12.5px; background: transparent; }
QLabel#fbNotifGo { color: %INK3%; font-size: 17px; font-weight: 700; }
QFrame#fbNotifRow:hover QLabel#fbNotifGo { color: %BRAND%; }
QLabel#fbNotifEmptyIcon {
    background: %BG_TINT%;
    border: 1px dashed %LINE2%;
    border-radius: 27px;
}
QLabel#fbNotifEmptyTitle { color: %INK2%; font-size: 14.5px; font-weight: 800; }
QFrame#fbNotifAlert {
    background: %DANGER_SOFT%;
    border: none;
    border-radius: 12px;
}
QLabel#fbNotifAlertText { color: %DANGER%; font-size: 12.5px; background: transparent; }
QLabel#fbNotifAlertMark {
    color: %DANGER%;
    font-size: 11px;
    font-weight: 900;
    border: 1px solid %DANGER%;
    border-radius: 8px;
    background: transparent;
}
QLabel#fbRowExcerpt { color: %INK2%; font-size: 12.5px; }

QLabel#fbFieldLabel { color: %INK2%; font-size: 13px; font-weight: 700; }
QLineEdit#fbInput, QPlainTextEdit#fbInput {
    background: %CARD%;
    color: %INK%;
    border: 1px solid %LINE2%;
    border-radius: 10px;
    padding: 8px 12px;
    font-size: 13.5px;
    selection-background-color: %BRAND%;
    selection-color: %ON_BRAND%;
}
QLineEdit#fbInput:focus, QPlainTextEdit#fbInput:focus { border-color: %BRAND%; }
QLineEdit#fbInput:disabled { color: %INK3%; }

QLabel#fbAuthTitle { color: %INK%; font-size: 21px; font-weight: 800; }
QLabel#fbAuthSub { color: %INK3%; font-size: 13px; }
QPushButton#fbPrimaryBlock {
    background: %BRAND%;
    color: %ON_BRAND%;
    border: 1px solid transparent;
    border-radius: 22px;
    font-size: 14.5px;
    font-weight: 700;
}
QPushButton#fbPrimaryBlock:hover { background: %BRAND_HOVER%; }
QPushButton#fbPrimaryBlock:disabled { background: %LINE2%; color: %INK3%; }
QPushButton#fbLink {
    background: transparent;
    color: %BRAND%;
    border: none;
    font-size: 13px;
    font-weight: 600;
    padding: 2px;
}
QPushButton#fbLink:hover { color: %BRAND_HOVER%; }
)QSS");

    auto put = [&qss](const char *token, const QString &value) {
        qss.replace(QString::fromLatin1(token), value);
    };
    put("%BRAND_HOVER%", p.brandHover);
    put("%BRAND_SOFT%", p.brandSoft);
    put("%BRAND_LINE%", p.brandLine);
    put("%BRAND%", p.brand);
    put("%ON_BRAND%", p.onBrand);
    put("%BG_TINT%", p.bgTint);
    // 引用块底色：浅色主题用浅底，深色主题用一层淡淡的高光，避免比卡片更黑显得像洞
    put("%QUOTE_BG%", p.dark ? QStringLiteral("rgba(255, 255, 255, 6%)") : p.bgTint);
    // 未读行里的引用块：浅色用白卡提亮，深色同样用更亮的高光，保持“浮起来”的关系
    put("%QUOTE_BG_ALT%", p.dark ? QStringLiteral("rgba(255, 255, 255, 10%)") : p.card);
    put("%WARN_SOFT%", p.warnSoft);
    put("%DANGER_SOFT%", p.dangerSoft);
    put("%DANGER%", p.danger);
    put("%INK3%", p.ink3);
    put("%INK2%", p.ink2);
    put("%INK%", p.ink);
    put("%LINE2%", p.line2);
    put("%LINE%", p.line);
    put("%CARD%", p.card);

    setStyleSheet(qss);

    // 头像占位色跟随主题
    if (!m_service->isLoggedIn())
        setAvatarPlaceholder(QString());
    else
        renderProfile();

    // 详情页的 3D 预览背景 / 模型动作图标也跟着主题走
    updateDetailBackground();
    refreshDetailPoseIcons();

    // 没有封面时封面横幅画的是主题色渐变，换主题要重画
    if (m_coverBanner)
        m_coverBanner->update();
    if (m_settingsCover)
        m_settingsCover->update();

    // 通知行的类型图标颜色（未读主题色/已读灰）跟着主题走，正在看通知页时重画
    if (m_activeTab == NotificationsTab && m_notificationsLoaded)
        renderNotifications();
}

// ════════════════════════════════════════════════════════════════ 视图

void ForumAccountPage::showOverview()
{
    if (!m_viewStack)
        return;
    // 详情页会把容器撑高，回到总览时恢复自适应
    m_viewStack->setMinimumHeight(0);
    m_viewStack->setCurrentIndex(OverviewView);
}

void ForumAccountPage::openAuth(AuthMode mode)
{
    if (m_authError)
    {
        m_authError->clear();
        m_authError->hide();
    }
    m_authNameEdit->clear();
    m_authPasswordEdit->clear();
    m_authConfirmEdit->clear();
    setAuthMode(mode);
    if (m_viewStack)
        m_viewStack->setCurrentIndex(AuthView);
    scrollToTop();
    QTimer::singleShot(0, this, [this]() { m_authNameEdit->setFocus(); });
}

void ForumAccountPage::setAuthMode(AuthMode mode)
{
    m_authMode = mode;
    const bool registering = (mode == RegisterMode);

    for (int i = 0; i < m_authTabButtons.size(); ++i)
        m_authTabButtons.at(i)->setChecked(i == int(mode));

    if (m_authTitle)
        m_authTitle->setText(registering ? tr("注册方块盒子论坛") : tr("登录方块盒子论坛"));
    if (m_authSub)
        m_authSub->setText(registering ? tr("取个名字，之后就能发帖和回复了。")
                                       : tr("用你注册的账号继续参与讨论。"));
    if (m_confirmField)
        m_confirmField->setVisible(registering);
    if (m_authSubmitBtn)
    {
        m_authSubmitBtn->setText(m_busy ? (registering ? tr("注册中…") : tr("登录中…"))
                                        : (registering ? tr("注册并登录") : tr("登录")));
        m_authSubmitBtn->setEnabled(!m_busy);
    }
    if (m_authError && !m_authError->text().isEmpty())
        m_authError->hide();
}

void ForumAccountPage::submitAuth()
{
    if (m_busy)
        return;

    const QString username = m_authNameEdit->text().trimmed();
    const QString password = m_authPasswordEdit->text();

    auto showError = [this](const QString &message) {
        m_authError->setText(message);
        m_authError->show();
    };

    if (username.isEmpty())
    {
        showError(tr("请输入用户名"));
        return;
    }
    if (password.length() < 6)
    {
        showError(tr("密码至少 6 位"));
        return;
    }
    if (m_authMode == RegisterMode && password != m_authConfirmEdit->text())
    {
        showError(tr("两次输入的密码不一样"));
        return;
    }

    m_authError->hide();
    m_busy = true;
    setAuthMode(m_authMode);

    if (m_authMode == RegisterMode)
        m_service->registerAccount(username, password);
    else
        m_service->login(username, password);
}

void ForumAccountPage::setActiveTab(TabIndex tab)
{
    m_activeTab = tab;
    for (int i = 0; i < m_tabButtons.size(); ++i)
        m_tabButtons.at(i)->setChecked(i == int(tab));

    showOverview();
    renderContentArea();
    if (m_service->isLoggedIn())
        loadTabData();
}

bool ForumAccountPage::returnToOverview()
{
    if (!m_viewStack)
        return false;

    // 账户详情页：退回「角色」页
    if (m_viewStack->currentIndex() == DetailView)
    {
        setDetailPose(SkinPose::Idle);
        m_activeTab = CharactersTab;
        showOverview();
        renderContentArea();
        scrollToTop();
        emit accountManagePageOpened();
        return true;
    }

    if (m_viewStack->currentIndex() != AuthView)
        return false;

    showOverview();
    m_authPasswordEdit->clear();
    m_authConfirmEdit->clear();
    if (m_authError)
    {
        m_authError->clear();
        m_authError->hide();
    }
    scrollToTop();
    emit accountManagePageOpened();
    return true;
}

void ForumAccountPage::onBackToAccountList()
{
    if (returnToOverview())
        return;

    showOverview();
    renderContentArea();
    scrollToTop();
    emit accountManagePageOpened();
}

void ForumAccountPage::openNotificationsTab()
{
    // setActiveTab 内部会回到总览视图并触发内容区与数据加载
    setActiveTab(NotificationsTab);
    scrollToTop();
}

void ForumAccountPage::openLoginForm()
{
    showOverview();
    openAuth(LoginMode);
    scrollToTop();
}

void ForumAccountPage::refreshAccount()
{
    m_threadsLoaded = m_repliesLoaded = m_bookmarksLoaded = false;
    m_threads = QJsonArray();
    m_replies = QJsonArray();
    m_bookmarks = QJsonArray();
    m_threadTotal = m_replyTotal = m_bookmarkTotal = 0;
    m_contentFavorites.clear();
    m_contentFavoritesLoaded = false;
    m_contentFavoriteTotal = 0;
    m_loadedAvatarPath.clear();

    m_service->refreshProfile();
    renderProfile();
    updateFooterNote();
    renderContentArea();

    if (m_service->isLoggedIn())
    {
        refreshBookmarkStat();
        loadTabData();
    }
}

void ForumAccountPage::scrollToTop()
{
    if (m_scroll && m_scroll->verticalScrollBar())
        m_scroll->verticalScrollBar()->setValue(0);
}

// ════════════════════════════════════════════════════════════════ 资料

void ForumAccountPage::renderProfile()
{
    const bool loggedIn = m_service->isLoggedIn();
    const ForumUser user = m_service->user();

    if (loggedIn)
    {
        m_nameLabel->setText(user.displayName);
        m_usernameLabel->setText(QStringLiteral("@") + user.username);

        if (user.bio.trimmed().isEmpty())
        {
            m_bioLabel->hide();
        }
        else
        {
            m_bioLabel->setText(user.bio);
            m_bioLabel->show();
        }

        QString roleText = tr("论坛成员");
        if (user.role == QStringLiteral("admin"))
            roleText = tr("管理员");
        else if (user.status == QStringLiteral("muted"))
            roleText = tr("已禁言");
        else if (user.status == QStringLiteral("banned"))
            roleText = tr("已封禁");
        m_roleChip->setText(roleText);
        m_roleChip->show();

        m_statThreads->setText(QString::number(user.threadCount));
        m_statPosts->setText(QString::number(user.postCount));
        m_statJoined->setText(user.createdAt > 0
                                  ? QDateTime::fromSecsSinceEpoch(user.createdAt)
                                        .toString(QStringLiteral("yyyy-MM-dd"))
                                  : QStringLiteral("—"));

        m_profileAuthActions->hide();
        m_profileUserActions->show();

        if (m_coverBanner)
            m_coverBanner->setCover(user.cover);
        if (m_settingsCover)
            m_settingsCover->setCover(user.cover);
        loadRemoteAvatar(user.avatar);
    }
    else
    {
        m_nameLabel->setText(tr("未登录"));
        m_usernameLabel->setText(tr("登录方块盒子论坛账号，发帖、回复、收藏都会同步到这里"));
        m_bioLabel->hide();
        m_roleChip->hide();

        m_statThreads->setText(QStringLiteral("0"));
        m_statPosts->setText(QStringLiteral("0"));
        m_statJoined->setText(QStringLiteral("—"));

        m_profileAuthActions->show();
        m_profileUserActions->hide();

        if (m_coverBanner)
            m_coverBanner->setCover(QString());
        if (m_settingsCover)
            m_settingsCover->setCover(QString());
        setAvatarPlaceholder(QString());
        m_loadedAvatarPath.clear();
    }

    m_statBookmarks->setText(QString::number(m_bookmarkTotal));

    // 本地游戏账户数量（论坛账号之外的 Minecraft 账户仍由经典界面管理）
    if (m_statGameAccounts && SettingsManager::instance())
    {
        const QList<AccountInfo> accounts = SettingsManager::instance()->getAccounts();
        m_statGameAccounts->setText(QString::number(accounts.count()));
    }

    if (m_settingsAvatar && m_loadedAvatarPath.isEmpty())
    {
        if (loggedIn)
            m_settingsAvatar->setPixmap(letterAvatar(user.displayName, 48, QColor(currentPalette().brand)));
        else
            m_settingsAvatar->setPixmap(guestAvatar(48, QColor(currentPalette().brand)));
    }

    if (m_settingsAccountLine)
    {
        if (loggedIn)
        {
            QString roleText = (user.role == QStringLiteral("admin")) ? tr("管理员") : tr("普通成员");
            m_settingsAccountLine->setText(
                tr("当前登录：%1（@%2） · %3 · 注册于 %4")
                    .arg(user.displayName, user.username, roleText,
                         user.createdAt > 0
                             ? QDateTime::fromSecsSinceEpoch(user.createdAt)
                                   .toString(QStringLiteral("yyyy-MM-dd"))
                             : tr("未知")));
        }
        else
        {
            m_settingsAccountLine->setText(tr("当前未登录论坛账号。"));
        }
    }
}

void ForumAccountPage::setAvatarPlaceholder(const QString &name)
{
    if (!m_avatarLabel)
        return;

    const QColor brand = QColor(currentPalette().brand);
    if (name.trimmed().isEmpty())
        m_avatarLabel->setPixmap(guestAvatar(kAvatarSize, brand));
    else
        m_avatarLabel->setPixmap(letterAvatar(name, kAvatarSize, brand));
}

void ForumAccountPage::setAvatarPixmap(const QPixmap &pixmap)
{
    if (!m_avatarLabel)
        return;

    const QPixmap rounded = roundPixmap(pixmap, kAvatarSize);
    if (rounded.isNull())
    {
        setAvatarPlaceholder(m_service->user().displayName);
        return;
    }
    m_avatarLabel->setPixmap(rounded);

    if (m_settingsAvatar)
        m_settingsAvatar->setPixmap(roundPixmap(pixmap, 48));
}

void ForumAccountPage::loadRemoteAvatar(const QString &avatarPath)
{
    const QString displayName = m_service->user().displayName;

    if (avatarPath.isEmpty())
    {
        setAvatarPlaceholder(displayName);
        m_loadedAvatarPath.clear();
        if (m_settingsAvatar && m_service->isLoggedIn())
            m_settingsAvatar->setPixmap(letterAvatar(displayName, 48, QColor(currentPalette().brand)));
        return;
    }

    if (m_loadedAvatarPath == avatarPath)
        return;
    m_loadedAvatarPath = avatarPath;

    QNetworkRequest request{QUrl(m_service->absoluteUrl(avatarPath))};
    request.setTransferTimeout(12000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, avatarPath]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            if (m_loadedAvatarPath == avatarPath)
                m_loadedAvatarPath.clear();
            setAvatarPlaceholder(m_service->user().displayName);
            return;
        }

        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll()))
        {
            setAvatarPlaceholder(m_service->user().displayName);
            return;
        }
        setAvatarPixmap(pixmap);
    });
}

// ════════════════════════════════════════════════════════════════ 角色页（游戏账户）

QPixmap ForumAccountPage::renderCharacterCover(const QImage &skin, int width, int height)
{
    if (width <= 0 || height <= 0)
        return QPixmap();

    const Palette p = currentPalette();

    QPixmap cover(width, height);
    cover.fill(Qt::transparent);

    QPainter painter(&cover);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 顶部两角跟随卡片圆角，底部直角
    const qreal radius = 14.0;
    QPainterPath clip;
    clip.moveTo(0, height);
    clip.lineTo(0, radius);
    clip.quadTo(0, 0, radius, 0);
    clip.lineTo(width - radius, 0);
    clip.quadTo(width, 0, width, radius);
    clip.lineTo(width, height);
    clip.closeSubpath();
    painter.setClipPath(clip);

    // 品牌绿渐变底
    const QColor brand(p.brand);
    QLinearGradient bg(0, 0, width * 0.35, height);
    bg.setColorAt(0.0, brand.lighter(p.dark ? 118 : 174));
    bg.setColorAt(1.0, brand.darker(p.dark ? 158 : 110));
    painter.fillRect(QRectF(0, 0, width, height), bg);

    // 柔和光斑
    QRadialGradient glow(QPointF(width * 0.5, height * 0.26), width * 0.9);
    glow.setColorAt(0.0, QColor(255, 255, 255, p.dark ? 26 : 78));
    glow.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(QRectF(0, 0, width, height), glow);

    // 正面皮肤模型
    const QImage model = composeFrontModel(skin);
    if (!model.isNull())
    {
        const int maxWidth = qMax(16, int(width * 0.60));
        const int maxHeight = qMax(32, int(height * 0.82));
        const int scale = qMax(1, qMin(maxWidth / 16, maxHeight / 32));
        const QSize target(16 * scale, 32 * scale);

        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawPixmap(QRect((width - target.width()) / 2, (height - target.height()) / 2,
                                 target.width(), target.height()),
                           QPixmap::fromImage(model));
    }

    // 底部轻微压暗，方便叠信息
    QLinearGradient shade(0, height * 0.70, 0, height);
    shade.setColorAt(0.0, QColor(0, 0, 0, 0));
    shade.setColorAt(1.0, QColor(0, 0, 0, p.dark ? 88 : 46));
    painter.fillRect(QRectF(0, height * 0.70, width, height * 0.30), shade);

    return cover;
}

QImage ForumAccountPage::skinForAccount(const QString &accountName, const QString &uuid)
{
    const auto cached = m_skinImages.constFind(accountName);
    if (cached != m_skinImages.constEnd() && !cached->isNull())
        return cached.value();

    QImage skin = SkinDownloader::loadSkinForAccount(accountName, uuid);
    if (skin.isNull())
        skin = SkinDownloader::getDefaultSkinForUser(accountName);
    return skin;
}

void ForumAccountPage::requestRemoteSkin(const QString &accountName, const QString &uuid,
                                         const QString &skinUrl)
{
    // 本地已有离线皮肤 / 下载缓存就不用再拉
    if (QFileInfo::exists(SkinDownloader::getOfflineSkinPath(accountName)))
        return;

    QString normalizedUuid = uuid;
    normalizedUuid.remove(QLatin1Char('-'));
    if (!normalizedUuid.isEmpty()
        && QFileInfo::exists(SkinDownloader::getCachePath(normalizedUuid)))
        return;

    QString url = skinUrl;
    if (url.isEmpty() && !normalizedUuid.isEmpty())
        url = QStringLiteral("https://crafatar.com/skins/") + normalizedUuid;
    if (url.isEmpty() || m_pendingSkinRequests.contains(accountName))
        return;

    m_pendingSkinRequests.insert(accountName);

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, accountName]() {
        reply->deleteLater();
        m_pendingSkinRequests.remove(accountName);

        if (reply->error() != QNetworkReply::NoError)
            return;

        QImage image;
        if (!image.loadFromData(reply->readAll()))
            return;

        m_skinImages.insert(accountName, image);

        QLabel *cover = m_characterCovers.value(accountName);
        const int height = m_characterCoverHeights.value(accountName, 0);
        if (cover && height > 0)
            cover->setPixmap(renderCharacterCover(image, qMax(1, cover->width()), height));
    });
}

void ForumAccountPage::reloadCharacters()
{
    if (!m_charactersLayout)
        return;

    // 清空旧卡片
    while (QLayoutItem *item = m_charactersLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_characterCovers.clear();
    m_characterCoverHeights.clear();

    if (!SettingsManager::instance())
        return;

    const QList<AccountInfo> accounts = SettingsManager::instance()->getAccounts();
    QWidget *host = m_charactersLayout->parentWidget();

    // 资料卡上的「游戏账户」数量一起刷新
    if (m_statGameAccounts)
        m_statGameAccounts->setText(QString::number(accounts.size()));

    if (accounts.isEmpty())
    {
        auto *empty = new QFrame(host);
        empty->setObjectName(QStringLiteral("fbCharEmpty"));
        empty->setMinimumWidth(360);
        auto *emptyLayout = new QVBoxLayout(empty);
        emptyLayout->setContentsMargins(26, 24, 26, 24);
        emptyLayout->setSpacing(8);

        auto *title = new QLabel(tr("还没有游戏账户"), empty);
        title->setObjectName(QStringLiteral("fbSectionTitle"));
        emptyLayout->addWidget(title);

        auto *hint = makeMutedLabel(
            tr("离线、微软正版和第三方账户都在经典账户管理页里添加；加好之后回来这里就能像卡片一样切换。"),
            empty, "fbEmpty");
        emptyLayout->addWidget(hint);

        auto *addBtn = new QPushButton(tr("去添加游戏账户"), empty);
        addBtn->setObjectName(QStringLiteral("fbBtnPrimary"));
        addBtn->setFixedHeight(34);
        addBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        addBtn->setCursor(Qt::PointingHandCursor);
        connect(addBtn, &QPushButton::clicked, this, []() {
            if (SettingsManager::instance())
                SettingsManager::instance()->setExperimentalForumAccountEnabled(false);
        });
        emptyLayout->addWidget(addBtn);

        m_charactersLayout->addWidget(empty);
        setTabHint(tr("还没有游戏账户"));
        return;
    }

    // 当前账户：默认账户；没有默认就取第一个
    QString currentName;
    for (const AccountInfo &account : accounts)
    {
        if (account.isDefault)
        {
            currentName = account.username;
            break;
        }
    }
    if (currentName.isEmpty())
        currentName = accounts.first().username;

    for (const AccountInfo &account : accounts)
    {
        const bool inUse = (account.username == currentName);

        auto *card = new ClickableFrame(host);
        card->setObjectName(QStringLiteral("fbCharCard"));
        card->setFixedWidth(214);
        card->setProperty("inUse", inUse);

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);

        // 封面：所有卡片统一高度，整排对齐
        const int coverHeight = kCharacterCoverHeight;
        auto *cover = new QLabel(card);
        cover->setFixedHeight(coverHeight);
        cover->setAlignment(Qt::AlignCenter);
        cover->setPixmap(renderCharacterCover(skinForAccount(account.username, account.uuid), 214,
                                              coverHeight));
        cardLayout->addWidget(cover);
        m_characterCovers.insert(account.username, cover);
        m_characterCoverHeights.insert(account.username, coverHeight);

        // 信息区
        auto *body = new QWidget(card);
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(12, 10, 12, 12);
        bodyLayout->setSpacing(6);

        auto *nameLabel = new QLabel(account.username, body);
        nameLabel->setObjectName(QStringLiteral("fbCharName"));
        bodyLayout->addWidget(nameLabel);

        auto *metaRow = new QHBoxLayout();
        metaRow->setContentsMargins(0, 0, 0, 0);
        metaRow->setSpacing(6);

        auto *typeChip = new QLabel(accountTypeText(account), body);
        typeChip->setObjectName(QStringLiteral("fbRowChip"));
        metaRow->addWidget(typeChip);
        metaRow->addStretch();

        auto *stateLabel = new QLabel(inUse ? tr("使用中") : tr("点击切换"), body);
        stateLabel->setObjectName(inUse ? QStringLiteral("fbChip") : QStringLiteral("fbNote"));
        metaRow->addWidget(stateLabel);
        bodyLayout->addLayout(metaRow);

        cardLayout->addWidget(body);

        const QString accountName = account.username;
        card->onClick = [this, accountName]() { switchGameAccount(accountName); };
        card->onDoubleClick = [this, accountName]() { openAccountDetail(accountName); };
        card->setToolTip(tr("%1\n单击切换为该账户，双击查看账户详情").arg(accountName));

        m_charactersLayout->addWidget(card);

        // 本地没有皮肤就异步拉一张，刷新这张卡片
        requestRemoteSkin(account.username, account.uuid, account.skinUrl);
    }

    setTabHint(tr("共 %1 个游戏账户").arg(accounts.size()));
}

void ForumAccountPage::switchGameAccount(const QString &accountName)
{
    if (!SettingsManager::instance() || accountName.isEmpty())
        return;

    QString uuid;
    for (const AccountInfo &account : SettingsManager::instance()->getAccounts())
    {
        if (account.username == accountName)
        {
            uuid = account.uuid;
            break;
        }
    }

    SettingsManager::instance()->setDefaultAccount(accountName);
    reloadCharacters();

    const QImage skin = skinForAccount(accountName, uuid);
    QPixmap avatar;
    if (!skin.isNull())
        avatar = QPixmap::fromImage(SkinDownloader::cropHeadPortrait(skin, 64));

    emit gameAccountSelected(accountName, avatar);
    NotificationManager::showSuccess(this, tr("已切换到游戏账户：%1").arg(accountName));
}

// ════════════════════════════════════════════════════════════════ 账户详情页

QWidget *ForumAccountPage::buildAccountDetailView(QWidget *parent)
{
    m_detailPage = new QWidget(parent);
    m_detailPage->setFixedHeight(620);

    auto *outer = new QVBoxLayout(m_detailPage);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(10);

    // 返回按钮不再放这里：顶栏已经有返回键，重复一个反而乱

    auto *bodyRow = new QHBoxLayout();
    bodyRow->setContentsMargins(0, 0, 0, 0);
    bodyRow->setSpacing(12);

    // 左：3D 皮肤预览（不画底色，模型直接浮在页面背景上）
    auto *previewCard = new QWidget(m_detailPage);
    previewCard->setObjectName(QStringLiteral("fbDetailPreview"));
    previewCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(0);

    m_detailSkinWidget = new Skin3DWidget(previewCard);
    m_detailSkinWidget->setObjectName(QStringLiteral("fbDetailSkin"));
    m_detailSkinWidget->setMinimumSize(280, 430);
    m_detailSkinWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QSurfaceFormat glFormat = m_detailSkinWidget->format();
    glFormat.setAlphaBufferSize(8);
    m_detailSkinWidget->setFormat(glFormat);
    m_detailSkinWidget->installEventFilter(this);
    previewLayout->addWidget(m_detailSkinWidget);
    bodyRow->addWidget(previewCard, 1);

    // 右：账户资料 + 模型动作 + 操作（与经典账户管理页右侧一致）
    auto *panelCard = new QFrame(m_detailPage);
    panelCard->setObjectName(QStringLiteral("fbDetailPanel"));
    auto *panelLayout = new QVBoxLayout(panelCard);
    panelLayout->setContentsMargins(16, 16, 16, 16);
    panelLayout->setSpacing(10);

    m_detailName = new QLabel(panelCard);
    m_detailName->setObjectName(QStringLiteral("fbDetailName"));
    m_detailName->setWordWrap(true);
    panelLayout->addWidget(m_detailName);

    auto *typeRow = new QHBoxLayout();
    typeRow->setContentsMargins(0, 0, 0, 0);
    typeRow->setSpacing(6);

    m_detailTypeDot = new QLabel(panelCard);
    m_detailTypeDot->setFixedSize(8, 8);
    typeRow->addWidget(m_detailTypeDot, 0, Qt::AlignVCenter);

    m_detailTypeLabel = new QLabel(panelCard);
    m_detailTypeLabel->setObjectName(QStringLiteral("fbNote"));
    typeRow->addWidget(m_detailTypeLabel);

    m_detailDefaultChip = new QLabel(tr("默认账户"), panelCard);
    m_detailDefaultChip->setObjectName(QStringLiteral("fbChip"));
    m_detailDefaultChip->hide();
    typeRow->addWidget(m_detailDefaultChip);
    typeRow->addStretch();
    panelLayout->addLayout(typeRow);

    m_detailMeta = new QLabel(panelCard);
    m_detailMeta->setObjectName(QStringLiteral("fbDetailMeta"));
    m_detailMeta->setWordWrap(true);
    panelLayout->addWidget(m_detailMeta);

    panelLayout->addSpacing(4);
    auto *modelTitle = new QLabel(tr("模型动作"), panelCard);
    modelTitle->setObjectName(QStringLiteral("fbSectionTitle"));
    panelLayout->addWidget(modelTitle);

    auto *poseGrid = new QGridLayout();
    poseGrid->setContentsMargins(0, 0, 0, 0);
    poseGrid->setHorizontalSpacing(6);
    poseGrid->setVerticalSpacing(6);

    const struct
    {
        QString text;
        const char *icon;
        const char *data;
    } poses[] = {
        {tr("待机"), ":/Images/Icons/pose_idle.svg", "pose_idle"},
        {tr("挥手"), ":/Images/Icons/pose_wave.svg", "pose_wave"},
        {tr("走路"), ":/Images/Icons/pose_walk.svg", "pose_walk"},
        {tr("跑步"), ":/Images/Icons/pose_run.svg", "pose_run"},
        {tr("潜行"), ":/Images/Icons/pose_sneak.svg", "pose_sneak"},
        {tr("游泳"), ":/Images/Icons/pose_swim.svg", "pose_swim"},
        {tr("睡觉"), ":/Images/Icons/pose_sleep.svg", "pose_sleep"},
        {tr("攻击"), ":/Images/Icons/pose_attack.svg", "pose_attack"},
        {tr("舞蹈"), ":/Images/Icons/pose_dance.svg", "pose_dance"},
        {tr("欢呼"), ":/Images/Icons/pose_cheer.svg", "pose_cheer"},
        {tr("骑马坐姿"), ":/Images/Icons/pose_ride.svg", "pose_ride"},
    };
    constexpr int kPoseCount = int(sizeof(poses) / sizeof(poses[0]));
    constexpr int kSpanIndex = kPoseCount - 1;

    for (int i = 0; i < kPoseCount; ++i)
    {
        auto *poseBtn = new QPushButton(poses[i].text, panelCard);
        poseBtn->setObjectName(QStringLiteral("fbPoseButton"));
        poseBtn->setProperty("iconPath", poses[i].icon);
        poseBtn->setProperty("poseData", poses[i].data);
        poseBtn->setIcon(IconHelper::loadColoredIcon(poses[i].icon, detailPoseIconColor(false), 16));
        poseBtn->setIconSize(QSize(16, 16));
        poseBtn->setCheckable(true);
        poseBtn->setChecked(i == 0);
        poseBtn->setFixedHeight(30);
        poseBtn->setCursor(Qt::PointingHandCursor);
        connect(poseBtn, &QPushButton::clicked, this, [this, poseBtn]() {
            const QString data = poseBtn->property("poseData").toString();
            SkinPose pose = SkinPose::Idle;
            if (data == QStringLiteral("pose_wave"))
                pose = SkinPose::Wave;
            else if (data == QStringLiteral("pose_walk"))
                pose = SkinPose::Walk;
            else if (data == QStringLiteral("pose_run"))
                pose = SkinPose::Run;
            else if (data == QStringLiteral("pose_sneak"))
                pose = SkinPose::Sneak;
            else if (data == QStringLiteral("pose_swim"))
                pose = SkinPose::Swim;
            else if (data == QStringLiteral("pose_sleep"))
                pose = SkinPose::Sleep;
            else if (data == QStringLiteral("pose_attack"))
                pose = SkinPose::Attack;
            else if (data == QStringLiteral("pose_dance"))
                pose = SkinPose::Dance;
            else if (data == QStringLiteral("pose_cheer"))
                pose = SkinPose::Cheer;
            else if (data == QStringLiteral("pose_ride"))
                pose = SkinPose::Ride;
            setDetailPose(pose);
        });
        m_detailPoseButtons.append(poseBtn);

        if (i == kSpanIndex)
            poseGrid->addWidget(poseBtn, i / 2, 0, 1, 2);
        else
            poseGrid->addWidget(poseBtn, i / 2, i % 2);
    }
    poseGrid->setColumnStretch(0, 1);
    poseGrid->setColumnStretch(1, 1);
    panelLayout->addLayout(poseGrid);

    panelLayout->addStretch();

    m_detailUseBtn = new QPushButton(tr("设为当前账户"), panelCard);
    m_detailUseBtn->setObjectName(QStringLiteral("fbDetailUse"));
    m_detailUseBtn->setFixedHeight(38);
    m_detailUseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_detailUseBtn, &QPushButton::clicked, this, &ForumAccountPage::useDetailAccountAsCurrent);
    panelLayout->addWidget(m_detailUseBtn);

    m_detailDeleteBtn = new QPushButton(tr("删除账户"), panelCard);
    m_detailDeleteBtn->setObjectName(QStringLiteral("fbDangerGhost"));
    m_detailDeleteBtn->setFixedHeight(34);
    m_detailDeleteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_detailDeleteBtn, &QPushButton::clicked, this, &ForumAccountPage::deleteDetailAccount);
    panelLayout->addWidget(m_detailDeleteBtn);

    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(6);

    m_detailActionCombo = new QComboBox(panelCard);
    m_detailActionCombo->setObjectName(QStringLiteral("fbDetailCombo"));
    m_detailActionCombo->addItem(tr("复制用户名"), "copy_username");
    m_detailActionCombo->addItem(tr("复制 UUID"), "copy_uuid");
    m_detailActionCombo->addItem(tr("刷新皮肤"), "refresh_skin");
    m_detailActionCombo->addItem(tr("打开皮肤编辑器"), "open_skin_editor");
    m_detailActionCombo->setCursor(Qt::PointingHandCursor);
    actionRow->addWidget(m_detailActionCombo, 1);

    m_detailExecuteBtn = new QPushButton(tr("执行"), panelCard);
    m_detailExecuteBtn->setObjectName(QStringLiteral("fbBtnGhost"));
    m_detailExecuteBtn->setFixedHeight(28);
    m_detailExecuteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_detailExecuteBtn, &QPushButton::clicked, this, &ForumAccountPage::executeDetailAction);
    actionRow->addWidget(m_detailExecuteBtn);
    panelLayout->addLayout(actionRow);

    // 右侧白卡直接铺满整列高度：不再用滚动容器，卡片下方不会露出页面底色
    panelCard->setFixedWidth(318);
    panelCard->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    bodyRow->addWidget(panelCard, 0);

    outer->addLayout(bodyRow, 1);

    return m_detailPage;
}

void ForumAccountPage::openAccountDetail(const QString &accountName)
{
    if (accountName.isEmpty())
        return;

    m_detailAccountName = accountName;
    refreshAccountDetail();
    if (m_viewStack)
        // 详情页比总览高：先把容器撑到详情页高度，避免被上一页的尺寸裁掉
    m_viewStack->setMinimumHeight(m_detailPage ? m_detailPage->height() : 620);
    m_viewStack->setCurrentIndex(DetailView);
    setDetailPose(SkinPose::Idle);
    // 切换视图后尺寸才就绪，下一轮事件循环再按视口调整相机距离
    QTimer::singleShot(220, this, [this]() {
        if (m_viewStack && m_detailPage)
            m_viewStack->setMinimumHeight(m_detailPage->height());
        fitDetailCamera();
        if (m_viewStack)
            m_viewStack->updateGeometry();
    });
    scrollToTop();
    emit accountManagePageOpened();
}

void ForumAccountPage::refreshAccountDetail()
{
    if (!SettingsManager::instance() || m_detailAccountName.isEmpty())
        return;

    AccountInfo info;
    bool found = false;
    for (const AccountInfo &account : SettingsManager::instance()->getAccounts())
    {
        if (account.username == m_detailAccountName)
        {
            info = account;
            found = true;
            break;
        }
    }

    if (!found)
    {
        // 账户已经不存在（被删掉了）：退回角色页
        setActiveTab(CharactersTab);
        return;
    }

    m_detailAccountType = info.type;
    m_detailAccountUuid = info.uuid;
    m_detailAccountSkinUrl = info.skinUrl;
    m_detailIsDefault = info.isDefault;

    if (m_detailName)
        m_detailName->setText(info.username);

    const QString typeText = accountTypeText(info);
    if (m_detailTypeLabel)
        m_detailTypeLabel->setText(typeText);
    if (m_detailTypeDot)
    {
        QColor dotColor(QStringLiteral("#F59E0B"));
        if (typeText.contains(tr("微软")))
            dotColor = QColor(QStringLiteral("#00A4EF"));
        else if (typeText.contains(tr("离线")))
            dotColor = QColor(QStringLiteral("#F59E0B"));
        else
            dotColor = QColor(QStringLiteral("#8B5CF6"));
        m_detailTypeDot->setStyleSheet(
            QStringLiteral("background-color:%1;border-radius:4px;").arg(dotColor.name()));
    }
    if (m_detailDefaultChip)
        m_detailDefaultChip->setVisible(info.isDefault);
    if (m_detailMeta)
    {
        QStringList parts;
        if (!info.uuid.isEmpty())
            parts << tr("UUID：%1").arg(info.uuid);
        if (!info.serverUrl.isEmpty())
            parts << tr("服务器：%1").arg(info.serverUrl);
        m_detailMeta->setText(parts.join(QStringLiteral("　")));
        m_detailMeta->setVisible(!parts.isEmpty());
    }
    if (m_detailUseBtn)
        m_detailUseBtn->setEnabled(!info.isDefault);

    // 皮肤：本地 / 缓存优先，缺了再异步拉
    QImage skin = SkinDownloader::loadSkinForAccount(info.username, info.uuid);
    if (skin.isNull())
        skin = SkinDownloader::getDefaultSkinForUser(info.username);
    if (m_detailSkinWidget && !skin.isNull())
        m_detailSkinWidget->setSkin(skin);
    requestDetailSkin(info.username, info.uuid, info.skinUrl);

    updateDetailBackground();
    refreshDetailPoseIcons();
}

void ForumAccountPage::requestDetailSkin(const QString &accountName, const QString &uuid,
                                         const QString &skinUrl)
{
    if (QFileInfo::exists(SkinDownloader::getOfflineSkinPath(accountName)))
        return;

    QString normalizedUuid = uuid;
    normalizedUuid.remove(QLatin1Char('-'));
    if (!normalizedUuid.isEmpty() && QFileInfo::exists(SkinDownloader::getCachePath(normalizedUuid)))
        return;

    QString url = skinUrl;
    if (url.isEmpty() && !normalizedUuid.isEmpty())
        url = QStringLiteral("https://crafatar.com/skins/") + normalizedUuid;
    if (url.isEmpty() || m_pendingSkinRequests.contains(QStringLiteral("detail:") + accountName))
        return;

    const QString key = QStringLiteral("detail:") + accountName;
    m_pendingSkinRequests.insert(key);

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BlockBoxLauncher"));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, accountName, key]() {
        reply->deleteLater();
        m_pendingSkinRequests.remove(key);
        if (reply->error() != QNetworkReply::NoError)
            return;
        QImage image;
        if (!image.loadFromData(reply->readAll()))
            return;

        m_skinImages.insert(accountName, image);
        if (accountName == m_detailAccountName && m_detailSkinWidget)
            m_detailSkinWidget->setSkin(image);
        if (QLabel *cover = m_characterCovers.value(accountName))
        {
            const int height = m_characterCoverHeights.value(accountName, 0);
            if (height > 0)
                cover->setPixmap(renderCharacterCover(image, qMax(1, cover->width()), height));
        }
    });
}

void ForumAccountPage::setDetailPose(SkinPose pose, const QString &tip)
{
    m_detailPose = pose;
    if (m_detailSkinWidget)
        m_detailSkinWidget->setPose(pose);

    QString poseData;
    switch (pose)
    {
    case SkinPose::Idle:   poseData = "pose_idle";   break;
    case SkinPose::Wave:   poseData = "pose_wave";   break;
    case SkinPose::Walk:   poseData = "pose_walk";   break;
    case SkinPose::Run:    poseData = "pose_run";    break;
    case SkinPose::Sneak:  poseData = "pose_sneak";  break;
    case SkinPose::Swim:   poseData = "pose_swim";   break;
    case SkinPose::Sleep:  poseData = "pose_sleep";  break;
    case SkinPose::Attack: poseData = "pose_attack"; break;
    case SkinPose::Dance:  poseData = "pose_dance";  break;
    case SkinPose::Cheer:  poseData = "pose_cheer";  break;
    case SkinPose::Ride:   poseData = "pose_ride";   break;
    }

    for (QPushButton *btn : m_detailPoseButtons)
    {
        const bool checked = (btn->property("poseData").toString() == poseData);
        if (btn->isChecked() != checked)
            btn->setChecked(checked);
        btn->setIcon(IconHelper::loadColoredIcon(btn->property("iconPath").toString(),
                                        detailPoseIconColor(checked)));
    }

    if (!tip.isEmpty())
        NotificationManager::showSuccess(this, tip);
}

void ForumAccountPage::refreshDetailPoseIcons()
{
    for (QPushButton *btn : m_detailPoseButtons)
    {
        const QString iconPath = btn->property("iconPath").toString();
        if (!iconPath.isEmpty())
            btn->setIcon(IconHelper::loadColoredIcon(iconPath, detailPoseIconColor(btn->isChecked())));
    }
}

QColor ForumAccountPage::detailPoseIconColor(bool checked) const
{
    if (checked)
        return QColor(currentPalette().brand);
    return currentPalette().dark ? QColor(QStringLiteral("#9CA3AF"))
                                 : QColor(QStringLiteral("#666666"));
}

void ForumAccountPage::useDetailAccountAsCurrent()
{
    if (m_detailAccountName.isEmpty())
        return;

    switchGameAccount(m_detailAccountName);
    refreshAccountDetail();
}

void ForumAccountPage::deleteDetailAccount()
{
    if (m_detailAccountName.isEmpty() || !SettingsManager::instance())
        return;

    const QString name = m_detailAccountName;
    const AppMessageBox::StandardButton reply = AppMessageBox::question(
        this, tr("确认删除"), tr("确定要删除游戏账户「%1」吗？").arg(name),
        AppMessageBox::Yes | AppMessageBox::No, AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    SettingsManager::instance()->removeAccount(name);
    m_detailAccountName.clear();
    setDetailPose(SkinPose::Idle);
    setActiveTab(CharactersTab);
    NotificationManager::showSuccess(this, tr("已删除游戏账户：%1").arg(name));
}

void ForumAccountPage::executeDetailAction()
{
    if (!m_detailActionCombo || m_detailAccountName.isEmpty())
        return;

    const QString action = m_detailActionCombo->currentData().toString();

    if (action == QStringLiteral("copy_username"))
    {
        QApplication::clipboard()->setText(m_detailAccountName);
        NotificationManager::showSuccess(this, tr("已复制用户名：%1").arg(m_detailAccountName));
    }
    else if (action == QStringLiteral("copy_uuid"))
    {
        if (m_detailAccountUuid.isEmpty())
        {
            NotificationManager::showError(this, tr("当前账户没有 UUID"));
            return;
        }
        QApplication::clipboard()->setText(m_detailAccountUuid);
        NotificationManager::showSuccess(this, tr("已复制 UUID：%1").arg(m_detailAccountUuid));
    }
    else if (action == QStringLiteral("refresh_skin"))
    {
        m_skinImages.remove(m_detailAccountName);
        QImage skin = SkinDownloader::loadSkinForAccount(m_detailAccountName, m_detailAccountUuid);
        if (skin.isNull())
            skin = SkinDownloader::getDefaultSkinForUser(m_detailAccountName);
        if (m_detailSkinWidget && !skin.isNull())
            m_detailSkinWidget->setSkin(skin);
        requestDetailSkin(m_detailAccountName, m_detailAccountUuid, m_detailAccountSkinUrl);
        NotificationManager::showSuccess(this, tr("已刷新皮肤"));
    }
    else if (action == QStringLiteral("open_skin_editor"))
    {
        emit skinEditorRequested();
    }
}

void ForumAccountPage::updateDetailBackground()
{
    if (!m_detailSkinWidget)
        return;

    const bool dark = ThemeManager::instance()
                      && ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme;
    const Palette p = currentPalette();
    m_detailSkinWidget->setThemeBackground(dark);
    // 左侧预览不再画灰底：背景直接用页面底色，模型看起来浮在页面上
    m_detailSkinWidget->setBackgroundColor(QColor(p.bg));
    m_detailSkinWidget->setForegroundColor(QColor(p.ink));
}

// ════════════════════════════════════════════════════════════════ 内容

void ForumAccountPage::showContentPage(int index)
{
    if (!m_contentLayout)
        return;
    if (index == m_contentPage)
        return;

    // 把当前页从布局里拿掉（控件仍归 host 所有，只隐藏）
    while (QLayoutItem *item = m_contentLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->hide();
        delete item;
    }

    m_contentPage = index;
    if (index < 0 || index >= m_contentPages.size())
        return;

    QWidget *page = m_contentPages.at(index);
    page->show();
    m_contentLayout->addWidget(page);
}

void ForumAccountPage::renderContentArea()
{
    if (!m_contentHost)
        return;

    // 「角色」页显示的是游戏账户，和论坛登录状态无关
    if (m_activeTab == CharactersTab)
    {
        showContentPage(CharactersContent);
        reloadCharacters();
        return;
    }

    const bool loggedIn = m_service->isLoggedIn();
    if (!loggedIn)
    {
        showContentPage(LoggedOutContent);
        setTabHint(tr("登录后可查看"));
        if (m_refreshBtn)
            m_refreshBtn->setEnabled(true);
        return;
    }

    if (m_refreshBtn)
        m_refreshBtn->setEnabled(true);

    switch (m_activeTab)
    {
    case CharactersTab:
        // 上面已经单独处理
        break;
    case ThreadsTab:
        showContentPage(ThreadsContent);
        renderThreads();
        break;
    case RepliesTab:
        showContentPage(RepliesContent);
        renderReplies();
        break;
    case BookmarksTab:
        showContentPage(BookmarksContent);
        renderBookmarks();
        break;
    case NotificationsTab:
        showContentPage(NotificationsContent);
        renderNotifications();
        break;
    case SettingsTab:
        showContentPage(SettingsContent);
        setTabHint(tr("改完记得保存"));
        fillSettingsForm();
        break;
    }
}

void ForumAccountPage::renderLoading(QVBoxLayout *layout)
{
    clearLayout(layout);
    auto *label = makeMutedLabel(tr("正在加载…"), layout->parentWidget(), "fbEmpty");
    label->setAlignment(Qt::AlignCenter);
    layout->addSpacing(24);
    layout->addWidget(label);
    layout->addSpacing(24);
    layout->addStretch();
}

void ForumAccountPage::renderThreads()
{
    clearLayout(m_threadListLayout);

    if (m_threads.isEmpty())
    {
        if (!m_threadsLoaded)
        {
            renderLoading(m_threadListLayout);
            return;
        }
        auto *empty = makeMutedLabel(tr("还没有发过主题，去论坛聊聊吧"), m_threadListLayout->parentWidget(),
                                     "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_threadListLayout->addSpacing(28);
        m_threadListLayout->addWidget(empty);
        m_threadListLayout->addStretch();
        setTabHint(tr("共 0 个主题"));
        return;
    }

    renderRows(m_threadListLayout, m_threads, 0);
    setTabHint(tr("共 %1 个主题").arg(m_threadTotal));
}

void ForumAccountPage::renderReplies()
{
    clearLayout(m_replyListLayout);

    if (m_replies.isEmpty())
    {
        if (!m_repliesLoaded)
        {
            renderLoading(m_replyListLayout);
            return;
        }
        auto *empty = makeMutedLabel(tr("还没有回复过帖子"), m_replyListLayout->parentWidget(), "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_replyListLayout->addSpacing(28);
        m_replyListLayout->addWidget(empty);
        m_replyListLayout->addStretch();
        setTabHint(tr("共 0 条回复"));
        return;
    }

    renderRows(m_replyListLayout, m_replies, 1);
    setTabHint(tr("共 %1 条回复").arg(m_replyTotal));
}

void ForumAccountPage::renderBookmarks()
{
    if (m_bookmarkSubTab == BookmarkResourceTab)
    {
        renderContentFavorites();
        return;
    }

    clearLayout(m_bookmarkListLayout);

    if (m_bookmarks.isEmpty())
    {
        if (!m_bookmarksLoaded)
        {
            renderLoading(m_bookmarkListLayout);
            return;
        }
        auto *empty = makeMutedLabel(tr("还没有收藏任何主题"), m_bookmarkListLayout->parentWidget(),
                                     "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_bookmarkListLayout->addSpacing(28);
        m_bookmarkListLayout->addWidget(empty);
        m_bookmarkListLayout->addStretch();
        setTabHint(tr("共 %1 个帖子收藏").arg(m_bookmarkTotal));
        return;
    }

    renderRows(m_bookmarkListLayout, m_bookmarks, 2);
    setTabHint(tr("共 %1 个帖子收藏").arg(m_bookmarkTotal));
}

void ForumAccountPage::setBookmarkSubTab(int subTab)
{
    if (m_bookmarkSubTab == subTab)
        return;
    m_bookmarkSubTab = subTab;
    renderBookmarks();
    loadTabData();
}

void ForumAccountPage::loadContentFavorites()
{
    if (!m_service->isLoggedIn())
        return;

    renderLoading(m_bookmarkListLayout);
    ForumContentService::instance()->fetchContentFavorites(
        1, 20,
        [this](const QVector<FavoriteItem> &items, int total) {
            m_contentFavorites = items;
            m_contentFavoriteTotal = total;
            m_contentFavoritesLoaded = true;
            if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkResourceTab)
                renderContentFavorites();
            refreshBookmarkStat();
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            m_contentFavoritesLoaded = true;
            clearLayout(m_bookmarkListLayout);
            auto *error =
                makeMutedLabel(message, m_bookmarkListLayout->parentWidget(), "fbEmpty");
            error->setAlignment(Qt::AlignCenter);
            m_bookmarkListLayout->addSpacing(24);
            m_bookmarkListLayout->addWidget(error);
            m_bookmarkListLayout->addStretch();
        });
}

void ForumAccountPage::requestContentIcon(const QString &url, QLabel *label)
{
    QPointer<QLabel> guard(label);
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(12000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, guard]() {
        reply->deleteLater();
        if (!guard || reply->error() != QNetworkReply::NoError)
            return;

        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll()))
            return;

        m_contentIconCache.insert(url, pixmap.scaled(40, 40, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
        guard->setPixmap(m_contentIconCache.value(url));
    });
}

void ForumAccountPage::renderContentFavorites()
{
    clearLayout(m_bookmarkListLayout);
    QWidget *host = m_bookmarkListLayout->parentWidget();
    const Palette p = currentPalette();

    if (m_contentFavorites.isEmpty())
    {
        if (!m_contentFavoritesLoaded)
        {
            renderLoading(m_bookmarkListLayout);
            return;
        }
        auto *empty = makeMutedLabel(tr("还没有收藏的资源，去「资源」页点 ☆ 收藏吧"), host, "fbEmpty");
        empty->setAlignment(Qt::AlignCenter);
        m_bookmarkListLayout->addSpacing(28);
        m_bookmarkListLayout->addWidget(empty);
        m_bookmarkListLayout->addStretch();
        setTabHint(tr("共 %1 个资源收藏").arg(m_contentFavoriteTotal));
        return;
    }

    for (const FavoriteItem &item : m_contentFavorites)
    {
        auto *row = new ClickableFrame(host);
        row->setObjectName(QStringLiteral("fbListRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 8, 10, 8);
        rowLayout->setSpacing(10);

        // 图标：缓存优先，未命中走异步下载，失败留字母头像
        auto *iconLabel = new QLabel(row);
        iconLabel->setObjectName(QStringLiteral("fbRowIcon"));
        iconLabel->setFixedSize(40, 40);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setScaledContents(false);
        if (!item.iconUrl.isEmpty() && m_contentIconCache.contains(item.iconUrl))
        {
            iconLabel->setPixmap(m_contentIconCache.value(item.iconUrl));
        }
        else if (!item.iconUrl.isEmpty())
        {
            iconLabel->setPixmap(letterAvatar(item.displayName(), 40, QColor(p.brand)));
            requestContentIcon(item.iconUrl, iconLabel);
        }
        else
        {
            iconLabel->setPixmap(letterAvatar(item.displayName(), 40, QColor(p.brand)));
        }
        rowLayout->addWidget(iconLabel);

        auto *contentLayout = new QVBoxLayout();
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(3);

        auto *titleLabel = new ElidedLabel(row);
        titleLabel->setObjectName(QStringLiteral("fbRowTitle"));
        const QString name = item.displayName();
        titleLabel->setFullText(name.isEmpty() ? tr("（未命名资源）") : name);
        titleLabel->setToolTip(name);
        contentLayout->addWidget(titleLabel);

        QString meta = item.displayType();
        if (item.source.compare(QStringLiteral("curseforge"), Qt::CaseInsensitive) == 0)
            meta += QStringLiteral(" · CurseForge");
        else if (item.source.compare(QStringLiteral("modrinth"), Qt::CaseInsensitive) == 0)
            meta += QStringLiteral(" · Modrinth");
        if (item.addedAt > 0)
            meta += QStringLiteral(" · ") + relativeTime(item.addedAt / 1000);
        auto *metaLabel = new QLabel(meta, row);
        metaLabel->setObjectName(QStringLiteral("fbRowMeta"));
        metaLabel->setWordWrap(true);
        contentLayout->addWidget(metaLabel);

        rowLayout->addLayout(contentLayout, 1);

        auto *removeBtn = new QPushButton(tr("移除"), row);
        removeBtn->setObjectName(QStringLiteral("fbBtnGhost"));
        removeBtn->setFixedHeight(28);
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setToolTip(tr("从账号收藏中移除（本地收藏夹同步删除）"));
        const FavoriteItem removed = item;
        connect(removeBtn, &QPushButton::clicked, this, [this, removed]() {
            removeAccountContentFavorite(removed);
        });
        rowLayout->addWidget(removeBtn);

        const ModInfo info = item.toModInfo();
        const int contentType = item.contentType;
        row->onClick = [this, info, contentType]() {
            emit contentFavoriteOpenRequested(info, static_cast<ContentType>(contentType));
        };

        m_bookmarkListLayout->addWidget(row);
    }

    auto *moreHint =
        makeMutedLabel(tr("只显示最近 20 条，更多收藏可在论坛网页版查看"), host, "fbNote");
    moreHint->setAlignment(Qt::AlignCenter);
    m_bookmarkListLayout->addSpacing(6);
    m_bookmarkListLayout->addWidget(moreHint);
    m_bookmarkListLayout->addStretch();
    setTabHint(tr("共 %1 个资源收藏").arg(m_contentFavoriteTotal));
}

void ForumAccountPage::removeAccountContentFavorite(const FavoriteItem &item)
{
    ForumContentService::instance()->removeContentFavorite(
        item.source, item.remoteId,
        [this, item]() {
            // 本地所有包含它的收藏夹一并移除，避免下次登录对账又传回云端
            FavoritesManager *favorites = FavoritesManager::instance();
            const QList<QString> folderIds = favorites->foldersContaining(item.id);
            for (const QString &folderId : folderIds)
                favorites->removeFavorite(folderId, item.id);

            // FavoriteItem 未定义 operator==，按全局唯一 id 移除
            for (int i = m_contentFavorites.size() - 1; i >= 0; --i)
            {
                if (m_contentFavorites.at(i).id == item.id)
                    m_contentFavorites.removeAt(i);
            }
            m_contentFavoriteTotal = qMax(0, m_contentFavoriteTotal - 1);
            if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkResourceTab)
                renderContentFavorites();
            refreshBookmarkStat();
            NotificationManager::showSuccess(this, tr("已从账号收藏中移除"));
        },
        [this](const QString &message, int) {
            NotificationManager::showError(this, message);
        });
}

void ForumAccountPage::refreshBookmarkStat()
{
    if (!m_service->isLoggedIn())
        return;

    // 「收藏」统计 = 论坛帖子收藏 + 资源收藏，两个轻量请求各取 total
    m_service->getJson(
        QStringLiteral("/api/me/bookmarks?per_page=1"),
        [this](const QJsonObject &root) {
            m_bookmarkTotal =
                root.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("total")).toInt();
            if (m_statBookmarks)
                m_statBookmarks->setText(QString::number(m_bookmarkTotal + m_contentFavoriteTotal));
            if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkThreadTab &&
                m_bookmarksLoaded)
                setTabHint(tr("共 %1 个帖子收藏").arg(m_bookmarkTotal));
        },
        nullptr);

    ForumContentService::instance()->fetchContentFavorites(
        1, 1,
        [this](const QVector<FavoriteItem> &, int total) {
            m_contentFavoriteTotal = total;
            if (m_statBookmarks)
                m_statBookmarks->setText(QString::number(m_bookmarkTotal + m_contentFavoriteTotal));
            if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkResourceTab &&
                m_contentFavoritesLoaded)
                setTabHint(tr("共 %1 个资源收藏").arg(m_contentFavoriteTotal));
        },
        nullptr);
}

QWidget *ForumAccountPage::buildNotificationsContent(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 14);
    layout->setSpacing(10);

    // 页头：未读统计 + 说明 + 全部已读。常驻在页面布局里，刷新列表时不重建，
    // 免得 clearLayout 清掉按钮后又重新 new 一个，留下残影。
    auto *head = new QWidget(page);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(9);

    auto *chip = new QLabel(head);
    chip->setObjectName(QStringLiteral("fbNotifUnreadChip"));
    chip->hide();
    headLayout->addWidget(chip, 0, Qt::AlignVCenter);

    auto *hint = new QLabel(tr("有人回复、提到你，或论坛发布公告时会出现在这里"), head);
    hint->setObjectName(QStringLiteral("fbNote"));
    headLayout->addWidget(hint, 1);

    m_markAllReadButton = new QPushButton(tr("全部已读"), head);
    m_markAllReadButton->setObjectName(QStringLiteral("fbBtnGhost"));
    m_markAllReadButton->setCursor(Qt::PointingHandCursor);
    m_markAllReadButton->setFixedHeight(28);
    connect(m_markAllReadButton, &QPushButton::clicked, this,
            &ForumAccountPage::markAllNotificationsRead);
    headLayout->addWidget(m_markAllReadButton, 0, Qt::AlignVCenter);

    layout->addWidget(head);

    // 列表容器：刷新时只重建这层内容
    auto *listHost = new QWidget(page);
    auto *listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);
    m_notificationListLayout = listLayout;

    layout->addWidget(listHost);
    layout->addStretch();
    return page;
}

void ForumAccountPage::loadNotifications()
{
    ForumContentService::instance()->fetchNotifications(
        1,
        [this](const QVector<ForumNotificationItem> &items, int unread, const ForumPagination &) {
            m_notifications = items;
            m_notificationsLoaded = true;
            Q_UNUSED(unread);
            if (m_activeTab == NotificationsTab)
                renderNotifications();
        },
        [this](const QString &message, int status) {
            Q_UNUSED(status);
            m_notificationsLoaded = true;
            if (!m_notificationListLayout)
                return;

            clearLayout(m_notificationListLayout);
            QWidget *host = m_notificationListLayout->parentWidget();

            if (auto *chip = findChild<QLabel *>(QStringLiteral("fbNotifUnreadChip")))
                chip->hide();
            if (m_markAllReadButton)
                m_markAllReadButton->setEnabled(false);
            setTabHint(tr("通知加载失败"));

            auto *error = new QLabel(message, host);
            error->setObjectName(QStringLiteral("fbNotifAlertText"));
            error->setWordWrap(true);
            error->setAlignment(Qt::AlignCenter);

            auto *retry = new QPushButton(tr("重新加载"), host);
            retry->setObjectName(QStringLiteral("fbBtnGhost"));
            retry->setCursor(Qt::PointingHandCursor);
            retry->setFixedHeight(30);
            connect(retry, &QPushButton::clicked, this, [this]() {
                m_notificationsLoaded = false;
                renderNotifications();
                loadNotifications();
            });

            auto *alert = new QFrame(host);
            alert->setObjectName(QStringLiteral("fbNotifAlert"));
            alert->setMaximumWidth(560);
            auto *alertLayout = new QHBoxLayout(alert);
            alertLayout->setContentsMargins(14, 12, 14, 12);
            alertLayout->setSpacing(10);

            auto *alertMark = new QLabel(QStringLiteral("!"), alert);
            alertMark->setObjectName(QStringLiteral("fbNotifAlertMark"));
            alertMark->setFixedSize(16, 16);
            alertMark->setAlignment(Qt::AlignCenter);
            alertLayout->addWidget(alertMark, 0, Qt::AlignTop);

            error->setParent(alert);
            alertLayout->addWidget(error, 1);
            retry->setParent(alert);
            alertLayout->addWidget(retry, 0, Qt::AlignVCenter);

            m_notificationListLayout->addSpacing(24);
            m_notificationListLayout->addWidget(alert, 0, Qt::AlignHCenter);
            m_notificationListLayout->addStretch();
        });
}

void ForumAccountPage::renderNotifications()
{
    if (!m_notificationListLayout)
        return;

    clearLayout(m_notificationListLayout);

    QWidget *host = m_notificationListLayout->parentWidget();
    const Palette pal = currentPalette();

    const int unreadRows = std::count_if(m_notifications.cbegin(), m_notifications.cend(),
                                         [](const ForumNotificationItem &item) {
                                             return item.unread;
                                         });

    // 页头统计与按钮状态：页头是常驻控件，这里只更新不重建
    if (auto *chip = findChild<QLabel *>(QStringLiteral("fbNotifUnreadChip")))
    {
        chip->setText(tr("%1 组未读").arg(unreadRows));
        chip->setVisible(unreadRows > 0);
    }
    if (m_markAllReadButton)
        m_markAllReadButton->setEnabled(unreadRows > 0);

    if (m_notifications.isEmpty())
    {
        if (!m_notificationsLoaded)
        {
            renderLoading(m_notificationListLayout);
            return;
        }

        // 空态：图标 + 标题 + 说明
        auto *box = new QWidget(host);
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(0, 0, 0, 0);
        boxLayout->setSpacing(8);

        auto *icon = new QLabel(box);
        icon->setObjectName(QStringLiteral("fbNotifEmptyIcon"));
        icon->setFixedSize(54, 54);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/announce.svg"),
                                                    QColor(pal.ink3), 22)
                            .pixmap(22, 22));
        boxLayout->addWidget(icon, 0, Qt::AlignHCenter);

        auto *title = new QLabel(tr("还没有通知"), box);
        title->setObjectName(QStringLiteral("fbNotifEmptyTitle"));
        title->setAlignment(Qt::AlignCenter);
        boxLayout->addWidget(title);

        auto *desc = makeMutedLabel(tr("有人回复你的主题、提到你，或论坛发布公告时，消息会收进这里"),
                                    box, "fbEmpty");
        desc->setAlignment(Qt::AlignCenter);
        boxLayout->addWidget(desc);

        m_notificationListLayout->addSpacing(30);
        m_notificationListLayout->addWidget(box);
        m_notificationListLayout->addStretch();
        setTabHint(tr("暂无通知"));
        return;
    }

    QString currentGroup;
    for (const ForumNotificationItem &item : m_notifications)
    {
        // 按日期分组：今天 / 昨天 / 具体日期
        const QString group = notificationDayGroup(item.createdAt);
        if (group != currentGroup)
        {
            currentGroup = group;
            auto *groupLabel = new QLabel(group, host);
            groupLabel->setObjectName(QStringLiteral("fbNotifGroup"));
            if (m_notificationListLayout->count() > 0)
                m_notificationListLayout->addSpacing(6);
            m_notificationListLayout->addWidget(groupLabel);
        }

        auto *row = new ClickableFrame(host);
        row->setObjectName(QStringLiteral("fbNotifRow"));
        row->setProperty("unread", item.unread ? QStringLiteral("true") : QStringLiteral("false"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 10, 12, 10);
        rowLayout->setSpacing(11);

        // 未读左侧强调条
        auto *accent = new QFrame(row);
        accent->setObjectName(QStringLiteral("fbNotifAccent"));
        accent->setFixedWidth(3);
        accent->setVisible(item.unread);
        rowLayout->addWidget(accent);

        // 左侧类型图标：回复 / 提及 / 系统，底色区分类型，未读用主题色
        QString iconPath;
        QString typeKey = QStringLiteral("reply");
        if (item.type == QStringLiteral("mention"))
        {
            iconPath = QStringLiteral(":/Images/Icons/sparkles.svg");
            typeKey = QStringLiteral("mention");
        }
        else if (item.type == QStringLiteral("system"))
        {
            iconPath = QStringLiteral(":/Images/Icons/announce.svg");
            typeKey = QStringLiteral("system");
        }
        else
        {
            iconPath = QStringLiteral(":/Images/Icons/send.svg");
        }

        auto *typeIcon = new QLabel(row);
        typeIcon->setObjectName(QStringLiteral("fbNotifIcon"));
        typeIcon->setProperty("type", typeKey);
        typeIcon->setFixedSize(36, 36);
        typeIcon->setAlignment(Qt::AlignCenter);
        typeIcon->setPixmap(IconHelper::loadColoredIcon(
                                iconPath,
                                item.unread ? QColor(pal.brand) : QColor(pal.ink3), 17)
                                .pixmap(17, 17));
        rowLayout->addWidget(typeIcon, 0, Qt::AlignTop);

        auto *contentLayout = new QVBoxLayout();
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(5);

        // 标题行：未读圆点 + 主题标题
        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(7);
        if (item.unread)
        {
            auto *dot = new QLabel(row);
            dot->setObjectName(QStringLiteral("fbNotifDot"));
            dot->setFixedSize(6, 6);
            titleRow->addWidget(dot, 0, Qt::AlignVCenter);
        }
        auto *title = new ElidedLabel(row);
        title->setObjectName(QStringLiteral("fbNotifTitle"));
        title->setFullText(item.threadTitle.isEmpty() ? tr("（无标题）") : item.threadTitle);
        title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        titleRow->addWidget(title, 1);
        contentLayout->addLayout(titleRow);

        // 副行：谁 + 干了什么，人名加粗
        QString actors = item.actorNames.isEmpty() ? QString() : item.actorNames.join(QStringLiteral("、"));
        if (item.actorNames.size() > 3)
            actors = item.actorNames.mid(0, 3).join(QStringLiteral("、")) +
                     tr(" 等 %1 人").arg(item.actorNames.size());

        QString action;
        if (item.type == QStringLiteral("mention"))
            action = tr("提到了你");
        else if (item.type == QStringLiteral("system"))
            action = tr("系统公告");
        else if (item.targetKind == QStringLiteral("post"))
            action = tr("回复了你的楼层");
        else
            action = tr("回复了你的主题");

        QString subText;
        if (actors.isEmpty())
            subText = QStringLiteral("<span style='color:%1'>%2</span>")
                          .arg(pal.ink2, action.toHtmlEscaped());
        else
            subText = QStringLiteral("<span style='color:%1;font-weight:700'>%2</span>"
                                     "<span style='color:%3'> %4</span>")
                          .arg(pal.ink, actors.toHtmlEscaped(), pal.ink2, action.toHtmlEscaped());

        auto *sub = new QLabel(row);
        sub->setObjectName(QStringLiteral("fbNotifSub"));
        sub->setTextFormat(Qt::RichText);
        sub->setText(subText);
        contentLayout->addWidget(sub);

        // 引用块：通知摘要原文
        if (!item.excerpt.isEmpty())
        {
            auto *quote = new QFrame(row);
            quote->setObjectName(QStringLiteral("fbNotifQuote"));
            auto *quoteLayout = new QHBoxLayout(quote);
            quoteLayout->setContentsMargins(9, 6, 9, 6);
            quoteLayout->setSpacing(0);
            auto *quoteText = new ElidedLabel(quote);
            quoteText->setObjectName(QStringLiteral("fbNotifQuoteText"));
            quoteText->setFullText(item.excerpt.simplified());
            quoteLayout->addWidget(quoteText, 1);
            contentLayout->addWidget(quote);
        }

        QStringList metaParts;
        if (item.count > 1)
            metaParts << tr("共 %1 条").arg(item.count);
        metaParts << relativeTime(item.createdAt);
        auto *meta = new QLabel(metaParts.join(QStringLiteral(" · ")), row);
        meta->setObjectName(QStringLiteral("fbRowMeta"));
        contentLayout->addWidget(meta);

        rowLayout->addLayout(contentLayout, 1);

        auto *go = new QLabel(QStringLiteral("›"), row);
        go->setObjectName(QStringLiteral("fbNotifGo"));
        rowLayout->addWidget(go, 0, Qt::AlignVCenter);

        const QVector<int> ids = item.ids;
        const int threadId = item.threadId;
        row->onClick = [this, ids, threadId]() {
            // 标已读（组内全部）然后直达帖子；回来后列表按服务端结果重新渲染
            ForumContentService::instance()->markNotificationsRead(
                ids, [this](int) {
                    m_service->refreshProfile();
                    m_notificationsLoaded = false;
                    if (m_activeTab == NotificationsTab)
                        loadNotifications();
                });
            if (threadId > 0)
                emit threadOpenRequested(threadId);
        };

        m_notificationListLayout->addWidget(row);
    }

    setTabHint(unreadRows > 0 ? tr("共 %1 条通知").arg(m_notifications.size())
                              : tr("没有未读通知"));
    m_notificationListLayout->addStretch();
}

void ForumAccountPage::markAllNotificationsRead()
{
    if (m_markAllReadButton)
        m_markAllReadButton->setEnabled(false);
    ForumContentService::instance()->markNotificationsRead(
        {},
        [this](int) {
            if (m_markAllReadButton)
                m_markAllReadButton->setEnabled(true);
            m_notificationsLoaded = false;
            m_service->refreshProfile();
            renderNotifications();
            loadNotifications();
        },
        [this](const QString &message, int) {
            Q_UNUSED(message);
            if (m_markAllReadButton)
                m_markAllReadButton->setEnabled(true);
        });
}

void ForumAccountPage::renderRows(QVBoxLayout *layout, const QJsonArray &items, int kind)
{
    QWidget *host = layout->parentWidget();

    for (const QJsonValue &value : items)
    {
        const QJsonObject item = value.toObject();

        auto *row = new ClickableFrame(host);
        row->setObjectName(QStringLiteral("fbListRow"));
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(10, 8, 10, 8);
        rowLayout->setSpacing(4);

        QString title;
        QString meta;
        int threadId = 0;

        if (kind == 1)
        {
            // 我的回复：帖子标题 + 板块 + 摘要
            threadId = item.value(QStringLiteral("thread_id")).toInt();
            title = item.value(QStringLiteral("thread_title")).toString();
            const QString board =
                item.value(QStringLiteral("board")).toObject().value(QStringLiteral("name")).toString();
            const QString snippet = item.value(QStringLiteral("snippet")).toString();
            meta = board;
            if (!snippet.isEmpty())
                meta += QStringLiteral(" · ") + snippet;
            const qint64 created =
                static_cast<qint64>(item.value(QStringLiteral("created_at")).toDouble());
            meta += QStringLiteral(" · ") + relativeTime(created);
        }
        else
        {
            // 我的主题 / 我的收藏：标题 + 板块 + 回复数 + 最近回复
            threadId = item.value(QStringLiteral("id")).toInt();
            title = item.value(QStringLiteral("title")).toString();
            const QString board =
                item.value(QStringLiteral("board")).toObject().value(QStringLiteral("name")).toString();
            const int replies = item.value(QStringLiteral("reply_count")).toInt();
            const qint64 lastPost =
                static_cast<qint64>(item.value(QStringLiteral("last_post_at")).toDouble());
            meta = board;
            if (!meta.isEmpty())
                meta += QStringLiteral(" · ");
            meta += tr("%1 条回复").arg(replies);
            if (lastPost > 0)
                meta += QStringLiteral(" · ") + relativeTime(lastPost);
        }

        auto *titleLabel = new ElidedLabel(row);
        titleLabel->setObjectName(QStringLiteral("fbRowTitle"));
        titleLabel->setFullText(title.isEmpty() ? tr("（无标题）") : title);
        titleLabel->setToolTip(title);
        rowLayout->addWidget(titleLabel);

        auto *metaLabel = new QLabel(meta, row);
        metaLabel->setObjectName(QStringLiteral("fbRowMeta"));
        metaLabel->setWordWrap(true);
        rowLayout->addWidget(metaLabel);

        // 深度集成：点击行时在启动器内直接打开帖子详情，不再跳浏览器
        row->onClick = [this, threadId]() {
            if (threadId > 0)
                emit threadOpenRequested(threadId);
        };

        layout->addWidget(row);
    }

    auto *moreHint = makeMutedLabel(tr("只显示最近 20 条，更多内容可在论坛网页版查看"), host, "fbNote");
    moreHint->setAlignment(Qt::AlignCenter);
    layout->addSpacing(6);
    layout->addWidget(moreHint);
    layout->addStretch();
}

void ForumAccountPage::loadTabData()
{
    if (!m_service->isLoggedIn())
        return;

    const int userId = m_service->user().id;

    if (m_activeTab == ThreadsTab && !m_threadsLoaded)
    {
        renderLoading(m_threadListLayout);
        m_service->getJson(
            QStringLiteral("/api/users/%1/threads?per_page=20").arg(userId),
            [this](const QJsonObject &root) {
                m_threads = root.value(QStringLiteral("threads")).toArray();
                m_threadTotal =
                    root.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("total")).toInt();
                m_threadsLoaded = true;
                if (m_activeTab == ThreadsTab)
                    renderThreads();
            },
            [this](const QString &message, int status) {
                Q_UNUSED(status);
                clearLayout(m_threadListLayout);
                auto *error = makeMutedLabel(message, m_threadListLayout->parentWidget(), "fbEmpty");
                error->setAlignment(Qt::AlignCenter);
                m_threadListLayout->addSpacing(24);
                m_threadListLayout->addWidget(error);
                m_threadListLayout->addStretch();
            });
    }
    else if (m_activeTab == RepliesTab && !m_repliesLoaded)
    {
        renderLoading(m_replyListLayout);
        m_service->getJson(
            QStringLiteral("/api/users/%1/posts?per_page=20").arg(userId),
            [this](const QJsonObject &root) {
                m_replies = root.value(QStringLiteral("posts")).toArray();
                m_replyTotal =
                    root.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("total")).toInt();
                m_repliesLoaded = true;
                if (m_activeTab == RepliesTab)
                    renderReplies();
            },
            [this](const QString &message, int status) {
                Q_UNUSED(status);
                clearLayout(m_replyListLayout);
                auto *error = makeMutedLabel(message, m_replyListLayout->parentWidget(), "fbEmpty");
                error->setAlignment(Qt::AlignCenter);
                m_replyListLayout->addSpacing(24);
                m_replyListLayout->addWidget(error);
                m_replyListLayout->addStretch();
            });
    }
    else if (m_activeTab == BookmarksTab)
    {
        if (m_bookmarkSubTab == BookmarkResourceTab)
        {
            if (!m_contentFavoritesLoaded)
                loadContentFavorites();
        }
        else if (!m_bookmarksLoaded)
        {
            renderLoading(m_bookmarkListLayout);
            m_service->getJson(
                QStringLiteral("/api/me/bookmarks?per_page=20"),
                [this](const QJsonObject &root) {
                    m_bookmarks = root.value(QStringLiteral("threads")).toArray();
                    m_bookmarkTotal =
                        root.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("total")).toInt();
                    m_bookmarksLoaded = true;
                    if (m_activeTab == BookmarksTab && m_bookmarkSubTab == BookmarkThreadTab)
                        renderBookmarks();
                    refreshBookmarkStat();
                },
                [this](const QString &message, int status) {
                    Q_UNUSED(status);
                    clearLayout(m_bookmarkListLayout);
                    auto *error = makeMutedLabel(message, m_bookmarkListLayout->parentWidget(), "fbEmpty");
                    error->setAlignment(Qt::AlignCenter);
                    m_bookmarkListLayout->addSpacing(24);
                    m_bookmarkListLayout->addWidget(error);
                    m_bookmarkListLayout->addStretch();
                });
        }
    }
    else if (m_activeTab == NotificationsTab && !m_notificationsLoaded)
    {
        renderLoading(m_notificationListLayout);
        loadNotifications();
    }
}

void ForumAccountPage::fillSettingsForm()
{
    const ForumUser user = m_service->user();
    if (!user.isValid())
        return;

    if (m_displayNameEdit)
        m_displayNameEdit->setText(user.displayName);
    if (m_emailEdit)
        m_emailEdit->setText(user.email);
    if (m_bioEdit)
        m_bioEdit->setPlainText(user.bio);
}

void ForumAccountPage::updateFooterNote()
{
    if (!m_footerNote)
        return;

    const QString url = m_service->baseUrl();
    const QString link = QStringLiteral("<a href=\"%1\" style=\"color:%2;text-decoration:none;\">%1</a>")
                             .arg(url.toHtmlEscaped(), currentPalette().brand);
    m_footerNote->setText(
        tr("账户来自方块盒子论坛 %1 · 这是实验性界面，图片、排序等细节可能和网页版略有出入；"
           "可在 设置 → 高级设置 → 实验性内容 里关闭。")
            .arg(link));
}

void ForumAccountPage::setTabHint(const QString &text)
{
    if (m_tabHint)
        m_tabHint->setText(text);
}

void ForumAccountPage::clearLayout(QVBoxLayout *layout)
{
    if (!layout)
        return;

    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

// ════════════════════════════════════════════════════════════════ 账号操作

void ForumAccountPage::saveProfileForm()
{
    if (!m_service->isLoggedIn())
        return;

    const QString displayName = m_displayNameEdit->text().trimmed();
    const QString email = m_emailEdit->text().trimmed();
    const QString bio = m_bioEdit->toPlainText().trimmed();

    if (bio.size() > 300)
    {
        NotificationManager::showError(this, tr("个人简介最多 300 个字"));
        return;
    }

    m_service->updateProfile(displayName, bio, email);
}

void ForumAccountPage::savePasswordForm()
{
    if (!m_service->isLoggedIn())
        return;

    const QString current = m_currentPasswordEdit->text();
    const QString next = m_newPasswordEdit->text();
    const QString confirm = m_confirmPasswordEdit->text();

    if (current.isEmpty())
    {
        NotificationManager::showError(this, tr("请先输入当前密码"));
        return;
    }
    if (next.length() < 6)
    {
        NotificationManager::showError(this, tr("新密码至少 6 位"));
        return;
    }
    if (next != confirm)
    {
        NotificationManager::showError(this, tr("两次输入的新密码不一样"));
        return;
    }

    m_service->changePassword(current, next);
    m_currentPasswordEdit->clear();
    m_newPasswordEdit->clear();
    m_confirmPasswordEdit->clear();
}

void ForumAccountPage::pickAvatarFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择头像"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.gif);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    m_service->uploadAvatar(path);
}

void ForumAccountPage::pickCoverFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择封面图"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.gif);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    m_service->uploadCover(path);
}

void ForumAccountPage::confirmLogout()
{
    if (!m_service->isLoggedIn())
        return;

    m_service->logout();
    m_threadsLoaded = m_repliesLoaded = m_bookmarksLoaded = false;
    m_threads = QJsonArray();
    m_replies = QJsonArray();
    m_bookmarks = QJsonArray();
    m_threadTotal = m_replyTotal = m_bookmarkTotal = 0;
    m_contentFavorites.clear();
    m_contentFavoritesLoaded = false;
    m_contentFavoriteTotal = 0;
    m_loadedAvatarPath.clear();

    setActiveTab(ThreadsTab);
    renderProfile();
    renderContentArea();
}
