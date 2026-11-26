/**
 * @file   MultiplayerLobbyPage.cpp
 * @brief  联机大厅页面实现：横幅概况、筛选栏、模组同款房间卡与空 / 出错状态
 * @author BlockBox Team
 * @date   2026-11-06
 *
 * 样式全部通过 StyleKit::resolve 解析 @TOKEN@ 令牌，跟随主题色与亮 / 暗主题
 * 即时重算；房间卡沿用「下载模组」等页的 modCardListItem / contentCard 体系，
 * 支持列表与瀑布流切换；单击卡片进入本页内叠着的房间详情页。
 */
#include "MultiplayerLobbyPage.h"

#include "components/ContentViewSwitch.h"
#include "components/ForumKit.h"
#include "components/MasonryContentCard.h"
#include "components/NotificationManager.h"
#include "layouts/FlowLayout.h"
#include "layouts/MasonryLayout.h"
#include "pages/MultiplayerLobbyDetailPage.h"
#include "utils/ForumAccountService.h"
#include "utils/ForumImageStore.h"
#include "utils/IconHelper.h"
#include "utils/Lobby/LobbyInstanceMatcher.h"
#include "utils/StyleKit.h"
#include "utils/Terracotta/TerracottaClient.h"
#include "utils/ThemeManager.h"
#include "utils/TouchScroll.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
QString elide(const QString &text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars - 1) + QStringLiteral("…");
}

QString roomTitle(const ForumLobbyRoom &room)
{
    const QString title = room.title.trimmed();
    return title.isEmpty() ? QObject::tr("未命名房间") : title;
}

QString hostDisplay(const ForumLobbyRoom &room)
{
    const QString display = room.host.displayName.trimmed();
    return display.isEmpty() ? room.host.username.trimmed() : display;
}

/** 卡片悬浮提示：列表模式看不到简介 / 房主，放到 tooltip 里 */
QString roomTooltip(const ForumLobbyRoom &room)
{
    QStringList lines;
    lines << roomTitle(room);

    QStringList meta;
    if (!room.gameVersion.isEmpty())
        meta << QObject::tr("MC %1").arg(room.gameVersion);
    if (!room.loader.isEmpty())
        meta << room.loader;
    const QString host = hostDisplay(room);
    if (!host.isEmpty())
        meta << QObject::tr("房主 %1").arg(host);
    if (!meta.isEmpty())
        lines << meta.join(QStringLiteral(" · "));

    const QString desc = room.description.trimmed();
    if (!desc.isEmpty())
        lines << elide(desc, 120);
    lines << QObject::tr("单击查看房间详情");
    return lines.join(QLatin1Char('\n'));
}

/** 核心类型 → 卡片图标上的单字徽记 */
QString coreGlyph(const ForumLobbyRoom &room)
{
    if (room.core == QLatin1String("terracotta"))
        return QStringLiteral("陶");
    if (room.core == QLatin1String("hongshi"))
        return QStringLiteral("红");
    if (room.core == QLatin1String("frp"))
        return QStringLiteral("穿");

    const QString label = room.coreLabel.trimmed().isEmpty()
                              ? ForumLobbyService::coreDisplayName(room.core)
                              : room.coreLabel.trimmed();
    return label.isEmpty() ? QStringLiteral("联") : label.left(1);
}

/** 房间状态 → 圆点文案 + 对应的样式名 */
struct RoomState
{
    QString text;
    QString objectName;
};

RoomState roomState(const ForumLobbyRoom &room)
{
    RoomState state;
    if (!room.isOnline())
    {
        state.text = QObject::tr("● 已下架");
        state.objectName = QStringLiteral("mlChipFull");
    }
    else if (room.status == QLatin1String("full"))
    {
        state.text = QObject::tr("● 已满");
        state.objectName = QStringLiteral("mlChipFull");
    }
    else if (room.status == QLatin1String("playing"))
    {
        state.text = QObject::tr("● 游戏中");
        state.objectName = QStringLiteral("mlChipPlaying");
    }
    else
    {
        state.text = QObject::tr("● 等待加入");
        state.objectName = QStringLiteral("mlChipOnline");
    }
    return state;
}

/** 房间封面 / 徽标的同色系渐变（与模组瀑布流卡同一套色板） */
void roomPalette(const ForumLobbyRoom &room, QColor &c1, QColor &c2)
{
    MasonryContentCard::paletteForSeed(
        QStringLiteral("lobby:%1:%2").arg(room.id).arg(room.core), c1, c2);
}

QPixmap roomBannerPixmap(const ForumLobbyRoom &room, int width, int height)
{
    QColor c1, c2;
    roomPalette(room, c1, c2);
    return MasonryContentCard::makeBannerPixmap(c1, c2, width, height);
}

QPixmap roomLogoPixmap(const ForumLobbyRoom &room, int size)
{
    QColor c1, c2;
    roomPalette(room, c1, c2);
    return MasonryContentCard::makeLogoPixmap(c1, c2, coreGlyph(room), size);
}

/** 封面异步回填：拿到真封面后把居中徽标收回右下角半压 */
void loadRoomCoverInto(QLabel *banner, QLabel *logo, const QString &coverPath, int width, int height)
{
    const QString path = coverPath.trimmed();
    if (!banner || path.isEmpty())
        return;
    const QString url = ForumAccountService::instance()->absoluteUrl(path);
    if (url.isEmpty())
        return;

    QPointer<QLabel> bannerGuard(banner);
    QPointer<QLabel> logoGuard(logo);
    ForumImageStore::instance()->requestImage(url, [bannerGuard, logoGuard, width, height](const QImage &image) {
        if (bannerGuard.isNull() || image.isNull())
            return;
        bannerGuard->setPixmap(
            MasonryContentCard::makeCoverPixmap(QPixmap::fromImage(image), width, height));
        bannerGuard->setProperty("gradientBanner", false);
        if (!logoGuard.isNull())
            MasonryContentCard::placeLogo(logoGuard, false, width, height);
    });
}

/** 列表卡标题：按完整标题给出稳定的期望宽度，避免省略后的文字把卡片越算越窄 */
class ListCardTitle : public ForumKit::ElidedLabel
{
public:
    explicit ListCardTitle(QWidget *parent)
        : ForumKit::ElidedLabel(parent)
    {
    }

    QSize sizeHint() const override
    {
        const QFontMetrics metrics(fontMetrics());
        const int width = qBound(80, metrics.horizontalAdvance(fullText()) + 2, 420);
        return QSize(width, QLabel::sizeHint().height());
    }

    QSize minimumSizeHint() const override
    {
        return QSize(80, QLabel::minimumSizeHint().height());
    }
};

/** 整张卡可点：单击进详情；卡内按钮自己吃掉点击，不会误触 */
class LobbyClickCard : public QFrame
{
public:
    explicit LobbyClickCard(QWidget *parent)
        : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        // 记下按下位置：拖动滚动条后再松手不算点击卡片
        if (event->button() == Qt::LeftButton)
            setProperty("pressPos", event->pos());
        else
            setProperty("pressPos", QVariant());
        QFrame::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const QVariant pressed = property("pressPos");
        setProperty("pressPos", QVariant());
        if (event->button() == Qt::LeftButton && onClick && pressed.isValid()
            && (event->pos() - pressed.toPoint()).manhattanLength() < 8
            && rect().contains(event->pos()))
            onClick();
        QFrame::mouseReleaseEvent(event);
    }
};

/** 列表卡：窗口很窄时收起邀请码和复制按钮，避免把标题挤出去 */
class LobbyListCard : public LobbyClickCard
{
public:
    explicit LobbyListCard(QWidget *parent)
        : LobbyClickCard(parent)
    {
    }

    void setCollapsibleExtras(QWidget *code, QWidget *copy, QWidget *chips,
                              QWidget *mine = nullptr)
    {
        m_code = code;
        m_copy = copy;
        m_chips = chips;
        m_mine = mine;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        LobbyClickCard::resizeEvent(event);

        // 三档：
        //   宽：标签 / 我的房间 / 邀请码 / 复制 全都要；
        //   中：让出标签行，保留邀请码和复制；
        //   窄：只留标题、状态和「加入」，其余进详情页看。
        const int w = width();
        if (w <= 0)
            return;
        const bool showChips = w >= 780;
        const bool showCode = w >= 560;
        if (m_chips)
            m_chips->setVisible(showChips);
        if (m_mine)
            m_mine->setVisible(showChips);
        if (m_code)
            m_code->setVisible(showCode);
        if (m_copy)
            m_copy->setVisible(showCode);
    }

private:
    QWidget *m_code = nullptr;
    QWidget *m_copy = nullptr;
    QWidget *m_chips = nullptr;
    QWidget *m_mine = nullptr;
};
} // namespace

MultiplayerLobbyPage::MultiplayerLobbyPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("multiplayerLobbyPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    // 视图偏好与「下载模组」一致：默认瀑布流，按页面 key 记住
    m_viewMode = static_cast<int>(ContentViewSwitch::loadPersisted(
        QStringLiteral("multiplayer_lobby"), ContentViewSwitch::Masonry));

    buildUi();

    connect(ThemeManager::instance(), &ThemeManager::themeColorChanged,
            this, &MultiplayerLobbyPage::applyStyle);
    // 亮 / 暗主题互切时只发 themeChanged，内联样式里的文字与边框色要跟着重算
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this](ThemeManager::ThemeType) { applyStyle(); });

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(30000);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (isVisible())
            requestRooms();
    });
    m_refreshTimer->start();

    connect(TerracottaClient::instance(), &TerracottaClient::stateChanged,
            this, &MultiplayerLobbyPage::onTerracottaStateChanged);

    applyStyle();
}

MultiplayerLobbyPage::~MultiplayerLobbyPage() = default;

void MultiplayerLobbyPage::buildUi()
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(24, 20, 24, 16);
    m_rootLayout->setSpacing(14);

    // ---- 顶部横幅：标题 + 一句话说明 + 房间概况 + 两个入口 ----
    auto *hero = new QFrame(this);
    hero->setObjectName(QStringLiteral("mlHero"));
    m_hero = hero;

    m_heroLayout = new QBoxLayout(QBoxLayout::LeftToRight, hero);
    m_heroLayout->setContentsMargins(22, 18, 20, 18);
    m_heroLayout->setSpacing(16);

    auto *heroText = new QVBoxLayout();
    heroText->setSpacing(5);

    m_titleLabel = new QLabel(tr("联机大厅"), hero);
    m_titleLabel->setObjectName(QStringLiteral("mlLobbyTitle"));
    heroText->addWidget(m_titleLabel);

    auto *subtitle = new QLabel(
        tr("挑一个房间就能直接进游戏：自动选版本、加载器和模组都合适的本地实例。"),
        hero);
    subtitle->setObjectName(QStringLiteral("mlLobbySubtitle"));
    subtitle->setWordWrap(true);
    heroText->addWidget(subtitle);

    m_heroMetaLabel = new QLabel(hero);
    m_heroMetaLabel->setObjectName(QStringLiteral("mlHeroMeta"));
    m_heroMetaLabel->setWordWrap(true);
    heroText->addWidget(m_heroMetaLabel);

    m_heroLayout->addLayout(heroText, 1);

    m_heroActions = new QWidget(hero);
    auto *buttonRow = new QHBoxLayout(m_heroActions);
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(10);

    auto *webBtn = new QPushButton(tr("打开网页大厅"), m_heroActions);
    webBtn->setObjectName(QStringLiteral("mlHeroGhostBtn"));
    webBtn->setCursor(Qt::PointingHandCursor);
    webBtn->setFixedHeight(34);
    connect(webBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://forum.mcblockbox.top/lobby.html")));
    });
    buttonRow->addWidget(webBtn);

    auto *hostBtn = new QPushButton(tr("我要开房"), m_heroActions);
    hostBtn->setObjectName(QStringLiteral("mlHeroPrimaryBtn"));
    hostBtn->setCursor(Qt::PointingHandCursor);
    hostBtn->setFixedHeight(34);
    connect(hostBtn, &QPushButton::clicked, this,
            &MultiplayerLobbyPage::openInstanceAssistantRequested);
    buttonRow->addWidget(hostBtn);
    buttonRow->addStretch(1);

    m_heroLayout->addWidget(m_heroActions, 0);
    m_heroLayout->setAlignment(m_heroActions, Qt::AlignVCenter);

    m_rootLayout->addWidget(hero);

    // ---- 筛选栏 ----
    auto *filterBar = new QWidget(this);
    filterBar->setObjectName(QStringLiteral("mlFilterBar"));
    filterBar->setAttribute(Qt::WA_StyledBackground, true);
    m_filterBar = filterBar;

    auto *filterRow = new QHBoxLayout(filterBar);
    filterRow->setContentsMargins(12, 10, 14, 10);
    filterRow->setSpacing(10);

    m_searchEdit = new QLineEdit(filterBar);
    m_searchEdit->setObjectName(QStringLiteral("mlSearch"));
    m_searchEdit->setPlaceholderText(tr("搜房间名、简介或房主"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(32);
    m_searchEdit->setMinimumWidth(110);
    m_searchEdit->setMaximumWidth(420);
    m_searchIcon = m_searchEdit->addAction(
        IconHelper::loadColoredIcon(QStringLiteral(":/Images/Icons/search.svg"),
                                    QColor(ThemeManager::instance()->currentThemeColor()), 16),
        QLineEdit::LeadingPosition);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MultiplayerLobbyPage::requestRooms);
    filterRow->addWidget(m_searchEdit, 1);

    m_coreCombo = new QComboBox(filterBar);
    m_coreCombo->setObjectName(QStringLiteral("mlCoreCombo"));
    m_coreCombo->setFixedHeight(32);
    m_coreCombo->setMinimumWidth(92);
    m_coreCombo->addItem(tr("全部核心"), QString());
    m_coreCombo->addItem(tr("陶瓦联机"), QStringLiteral("terracotta"));
    m_coreCombo->addItem(tr("红石联机"), QStringLiteral("hongshi"));
    m_coreCombo->addItem(tr("内网穿透"), QStringLiteral("frp"));
    connect(m_coreCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { requestRooms(); });
    filterRow->addWidget(m_coreCombo);

    filterRow->addStretch(1);

    // 列表 / 瀑布流切换（与「下载模组」等页同款）
    m_viewSwitch = new ContentViewSwitch(filterBar);
    m_viewSwitch->setViewMode(static_cast<ContentViewSwitch::ViewMode>(m_viewMode));
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged, this,
            [this](ContentViewSwitch::ViewMode mode) { applyRoomViewMode(static_cast<int>(mode)); });
    filterRow->addWidget(m_viewSwitch);

    m_statusLabel = new QLabel(filterBar);
    m_statusLabel->setObjectName(QStringLiteral("mlStatus"));
    filterRow->addWidget(m_statusLabel);

    m_rootLayout->addWidget(filterBar);

    // ---- 房间列表 ----
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("mlScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    TouchScroll::enableDragScroll(m_scroll);

    m_container = new QWidget(m_scroll);
    m_container->setObjectName(QStringLiteral("mlContainer"));
    // 布局在 renderRooms 里按当前视图模式挂上
    m_scroll->setWidget(m_container);
    m_rootLayout->addWidget(m_scroll, 1);

    updateHeroMeta();
    setStatus(tr("加载中…"));
}

void MultiplayerLobbyPage::applyStyle()
{
    if (m_searchIcon)
        m_searchIcon->setIcon(IconHelper::loadColoredIcon(
            QStringLiteral(":/Images/Icons/search.svg"),
            QColor(ThemeManager::instance()->currentThemeColor()), 16));

    setStyleSheet(StyleKit::resolve(QStringLiteral(
        /* 页面本身不刷底色：自定义背景要能透出来，经典模式下透出主窗口底色 */
        "QWidget#multiplayerLobbyPage { background: transparent; }"

        /* 顶部横幅：主题色渐变，标题反白 */
        "QFrame#mlHero {"
        "  border: none; border-radius: 16px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "    stop:0 @PRIMARY_DARK@, stop:0.55 @PRIMARY@, stop:1 @PRIMARY_LIGHT@);"
        "}"
        "QLabel#mlLobbyTitle {"
        "  color: @TEXT_ON_PRIMARY@; font-size: 23px; font-weight: 800;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlLobbySubtitle {"
        "  color: rgba(255, 255, 255, 0.90); font-size: 13px;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlHeroMeta {"
        "  color: rgba(255, 255, 255, 0.78); font-size: 12px;"
        "  background: transparent; border: none;"
        "}"
        "QPushButton#mlHeroPrimaryBtn {"
        "  background: @TEXT_ON_PRIMARY@; color: @PRIMARY_DARK@; border: none;"
        "  border-radius: 9px; padding: 0 18px; font-size: 13px; font-weight: 700;"
        "}"
        "QPushButton#mlHeroPrimaryBtn:hover { background: rgba(255, 255, 255, 0.88); }"
        "QPushButton#mlHeroPrimaryBtn:pressed { background: rgba(255, 255, 255, 0.76); }"
        "QPushButton#mlHeroGhostBtn {"
        "  background: rgba(255, 255, 255, 0.16); color: @TEXT_ON_PRIMARY@;"
        "  border: 1px solid rgba(255, 255, 255, 0.42); border-radius: 9px;"
        "  padding: 0 14px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton#mlHeroGhostBtn:hover { background: rgba(255, 255, 255, 0.28); }"
        "QPushButton#mlHeroGhostBtn:pressed { background: rgba(255, 255, 255, 0.36); }"

        /* 筛选栏 */
        "QWidget#mlFilterBar {"
        "  background: @BG_GLASS@; border: 1px solid @BORDER@; border-radius: 12px;"
        "}"
        "QLineEdit#mlSearch {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; padding: 0 10px;"
        "  selection-background-color: @PRIMARY_RGBA_38@; selection-color: @TEXT_PRIMARY@;"
        "}"
        "QLineEdit#mlSearch:hover { border-color: @BORDER_HOVER@; }"
        "QLineEdit#mlSearch:focus { border-color: @PRIMARY@; }"
        "QComboBox#mlCoreCombo, QComboBox#mlSourceCombo {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; padding: 0 30px 0 12px;"
        "}"
        "QComboBox#mlCoreCombo:hover, QComboBox#mlSourceCombo:hover { border-color: @BORDER_HOVER@; }"
        "QComboBox#mlCoreCombo:focus, QComboBox#mlSourceCombo:focus { border: 1px solid @PRIMARY@; }"
        "QComboBox#mlCoreCombo::drop-down, QComboBox#mlSourceCombo::drop-down {"
        "  subcontrol-origin: padding; subcontrol-position: top right;"
        "  width: 26px; border: none; border-left: 1px solid @BORDER_LIGHT@;"
        "  border-top-right-radius: 9px; border-bottom-right-radius: 9px;"
        "  background: transparent;"
        "}"
        "QComboBox#mlCoreCombo::drop-down:hover, QComboBox#mlSourceCombo::drop-down:hover"
        " { background: @BG_HOVER@; }"
        "QComboBox#mlCoreCombo::down-arrow, QComboBox#mlSourceCombo::down-arrow {"
        "  image: url(:/Images/Icons/arrow_down_triangle.svg);"
        "  width: 10px; height: 10px; margin-right: 8px; border: none;"
        "}"
        "QComboBox#mlCoreCombo QAbstractItemView, QComboBox#mlSourceCombo QAbstractItemView {"
        "  background: @BG_CARD@; color: @TEXT_PRIMARY@; border: 1px solid @BORDER_STRONG@;"
        "  border-radius: 10px; padding: 4px; outline: none;"
        "  selection-background-color: @PRIMARY_BG@; selection-color: @TEXT_ACCENT@;"
        "}"
        "QLabel#mlStatus {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#mlStatus[error=\"true\"] { color: @DANGER_TEXT@; }"

        /* 房间卡：容器 / 文字 / 胶囊沿用模组卡（modCard* / contentCard*）体系，
           这里只补几个大厅专用的零件 */
        "QLabel#mlCardCode {"
        "  color: @TEXT_SECONDARY@; background: @BG_MUTED@;"
        "  border: 1px solid @BORDER_LIGHT@; border-radius: 7px; padding: 2px 8px;"
        "  font-family: Consolas, \"Microsoft YaHei\";"
        "  font-size: 12px; font-weight: 600;"
        "}"
        "QLabel#mlCardCode[empty=\"true\"] {"
        "  color: @TEXT_TERTIARY@; font-weight: 400; background: transparent; border-color: transparent;"
        "}"
        "QLabel#mlChipMine {"
        "  color: @TEXT_ACCENT@; background: @PRIMARY_RGBA_30@;"
        "  border: 1px solid @PRIMARY_RGBA_51@; border-radius: 9px; padding: 1px 8px;"
        "  font-size: 11px; font-weight: 700;"
        "}"
        "QLabel#mlChipOnline {"
        "  color: @SUCCESS@; background: rgba(16, 185, 129, 0.12);"
        "  border: 1px solid rgba(16, 185, 129, 0.30); border-radius: 8px;"
        "  padding: 1px 7px; font-size: 11px; font-weight: 700;"
        "}"
        "QLabel#mlChipPlaying {"
        "  color: @WARNING_TEXT@; background: rgba(245, 158, 11, 0.14);"
        "  border: 1px solid rgba(245, 158, 11, 0.32); border-radius: 8px;"
        "  padding: 1px 7px; font-size: 11px; font-weight: 700;"
        "}"
        "QLabel#mlChipFull {"
        "  color: @TEXT_TERTIARY@; background: @BG_MUTED@;"
        "  border: 1px solid @BORDER@; border-radius: 8px;"
        "  padding: 1px 7px; font-size: 11px; font-weight: 700;"
        "}"
        "QLabel#mlMasonryMeta {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; background: transparent; border: none;"
        "}"
        "QPushButton#mlJoinBtn {"
        "  background: @PRIMARY@; color: @TEXT_ON_PRIMARY@; border: none; border-radius: 9px;"
        "  padding: 0 14px; font-size: 13px; font-weight: 700;"
        "}"
        "QPushButton#mlJoinBtn:hover { background: @PRIMARY_HOVER@; }"
        "QPushButton#mlJoinBtn:pressed { background: @PRIMARY_PRESSED@; }"
        "QPushButton#mlJoinBtn:disabled { background: @BG_DISABLED@; color: @TEXT_DISABLED@; }"

        /* 空 / 加载中 / 连不上时的占位卡 */
        "QFrame#mlStateCard {"
        "  background: @BG_MUTED@; border: 1px dashed @BORDER_DASHED@; border-radius: 16px;"
        "}"
        "QLabel#mlStateGlyph { font-size: 32px; background: transparent; border: none; }"
        "QLabel#mlStateTitle {"
        "  color: @TEXT_PRIMARY@; font-size: 15px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlStateHint {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QPushButton#mlPrimaryBtn {"
        "  background: @PRIMARY@; color: @TEXT_ON_PRIMARY@; border: none; border-radius: 9px;"
        "  padding: 0 18px; font-size: 13px; font-weight: 700;"
        "}"
        "QPushButton#mlPrimaryBtn:hover { background: @PRIMARY_HOVER@; }"
        "QPushButton#mlPrimaryBtn:pressed { background: @PRIMARY_PRESSED@; }"

        /* 列表容器 / 详情页遮罩 */
        "QScrollArea#mlScroll, QWidget#mlContainer { background: transparent; border: none; }"
        "QWidget#mlDetailOverlay { background: transparent; }"
    )));

    // 换主题后图标颜色 / 瀑布流渐变要跟着重算，直接按当前数据重排一次
    if (m_loadedOnce && !m_rooms.isEmpty())
        renderRooms();
}

void MultiplayerLobbyPage::refresh()
{
    requestRooms();
}

void MultiplayerLobbyPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
    if (m_detailOverlay && m_detailOverlay->isVisible())
        m_detailOverlay->setGeometry(rect());
}

void MultiplayerLobbyPage::updateResponsiveLayout()
{
    if (!m_rootLayout || !m_heroLayout || !m_heroActions)
        return;

    // 手机 / 窄窗口：横幅改成上下排、页边距收窄、状态文字让位给视图切换
    const bool compact = width() > 0 && width() < 720;
    const bool changed = (compact != m_compactLayout);
    m_compactLayout = compact;

    if (compact)
    {
        m_heroLayout->setDirection(QBoxLayout::TopToBottom);
        m_heroLayout->setAlignment(m_heroActions, Qt::Alignment());
        m_rootLayout->setContentsMargins(14, 14, 14, 12);
        m_rootLayout->setSpacing(10);
    }
    else
    {
        m_heroLayout->setDirection(QBoxLayout::LeftToRight);
        m_heroLayout->setAlignment(m_heroActions, Qt::AlignVCenter);
        m_rootLayout->setContentsMargins(24, 20, 24, 16);
        m_rootLayout->setSpacing(14);
    }

    // 极窄时状态文字会把视图切换挤出去，先收起，筛选栏只留搜索 / 核心 / 切换
    if (m_statusLabel && changed)
        m_statusLabel->setVisible(!compact);

    // 窄屏下把几个控件的最低宽度都收一收，避免挤出屏幕
    if (m_searchEdit)
        m_searchEdit->setMinimumWidth(compact ? 60 : 110);
    if (m_coreCombo)
        m_coreCombo->setMinimumWidth(compact ? 80 : 92);
}

void MultiplayerLobbyPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_loading)
        requestRooms();
}

void MultiplayerLobbyPage::requestRooms()
{
    if (m_loading)
        return;
    m_loading = true;
    setStatus(tr("正在刷新…"));
    updateHeroMeta();

    // 先把本机实例扫进缓存：十几张房间卡都要用同一份结果，别每次点开都重新读盘
    LobbyInstanceMatcher::warmUp();
    m_instancePool = LobbyInstanceMatcher::scanInstances();

    // 新的一轮：上一轮还没回来的回调全部作废。
    // 上次成功拿到的房间先留着——这次没拉到时列表不至于整个清空
    ++m_fetchGen;
    const int gen = m_fetchGen;
    m_forumPending = false;
    m_forumError.clear();

    const QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    const QString core = m_coreCombo ? m_coreCombo->currentData().toString() : QString();

    // 论坛大厅：关键词 / 核心筛选都在服务端做
    m_forumPending = true;
    ForumLobbyService::instance()->fetchRooms(
        keyword, core,
        [this, gen](const QVector<ForumLobbyRoom> &rooms, int total) {
            if (gen != m_fetchGen)
                return;
            m_forumPending = false;
            m_forumRooms = rooms;
            m_forumTotal = total;
            m_loadedOnce = true;
            onSourceFetchSettled();
        },
        [this, gen](const QString &message, int) {
            if (gen != m_fetchGen)
                return;
            m_forumPending = false;
            m_forumError = message;
            m_loadedOnce = true;
            onSourceFetchSettled();
        });

    // 兜底：请求没发出去时也当作本轮结束去聚合一次
    onSourceFetchSettled();
}

void MultiplayerLobbyPage::onSourceFetchSettled()
{
    // 请求还没回来：等回调到了再聚合
    if (m_forumPending)
        return;

    m_loading = false;
    m_errorText = m_forumError;

    m_rooms = m_forumRooms;
    m_total = m_forumTotal;

    // renderRooms 里会顺带把「共 N 个房间」写进状态栏
    renderRooms();
    syncDetailRoom();

    // 有房间但这次没拉到：状态栏补一句，别让人以为列表就是最新的
    if (!m_rooms.isEmpty() && !m_errorText.isEmpty())
    {
        setStatus(tr("共 %1 个房间 · 大厅暂时连不上，先看上次的结果")
                      .arg(m_rooms.size()), true);
    }
}

void MultiplayerLobbyPage::updateHeroMeta()
{
    if (!m_heroMetaLabel)
        return;

    if (m_loading && !m_loadedOnce)
    {
        m_heroMetaLabel->setText(tr("正在连接大厅…"));
        return;
    }
    if (!m_errorText.isEmpty() && m_rooms.isEmpty())
    {
        m_heroMetaLabel->setText(tr("暂时连不上大厅，稍后会自动再试"));
        return;
    }
    if (m_rooms.isEmpty())
    {
        m_heroMetaLabel->setText(tr("现在还没有人在大厅开房"));
        return;
    }

    int playing = 0;
    int waiting = 0;
    for (const ForumLobbyRoom &room : m_rooms)
    {
        if (room.status == QLatin1String("playing"))
            ++playing;
        else
            ++waiting;
    }

    m_heroMetaLabel->setText(tr("在线 %1 个房间 · 等待加入 %2 个 · 游戏中 %3 个")
                                 .arg(m_rooms.size())
                                 .arg(waiting)
                                 .arg(playing));
}

void MultiplayerLobbyPage::clearRoomViews()
{
    for (QWidget *card : m_roomCards)
    {
        card->hide();
        card->deleteLater();
    }
    m_roomCards.clear();

    QLayout *oldLayout = m_listLayout ? static_cast<QLayout *>(m_listLayout)
                                      : static_cast<QLayout *>(m_masonryLayout);
    if (oldLayout)
    {
        while (QLayoutItem *item = oldLayout->takeAt(0))
            delete item;
        delete oldLayout;
    }
    m_listLayout = nullptr;
    m_masonryLayout = nullptr;

    // 旧的空状态卡是普通控件，不在 m_roomCards 里，这里一起摘掉
    for (QFrame *stale : m_container->findChildren<QFrame *>(QStringLiteral("mlStateCard")))
    {
        stale->hide();
        stale->deleteLater();
    }
}

void MultiplayerLobbyPage::renderRooms()
{
    if (!m_container)
        return;

    clearRoomViews();

    if (m_rooms.isEmpty())
    {
        // 空 / 加载中 / 连不上：在列表布局里居中放一张占位卡
        m_listLayout = new QVBoxLayout(m_container);
        m_listLayout->setContentsMargins(0, 12, 2, 12);
        m_listLayout->setSpacing(10);

        QWidget *state = nullptr;
        if (m_loading && !m_loadedOnce)
        {
            state = createStateCard(QStringLiteral("🛰"),
                                    tr("正在加载房间…"),
                                    tr("正在从联机大厅取房间列表，稍等一下就好。"),
                                    QString(), nullptr);
        }
        else if (!m_errorText.isEmpty())
        {
            state = createStateCard(QStringLiteral("📡"),
                                    tr("暂时连不上联机大厅"),
                                    tr("%1\n检查一下网络，或者稍后再试一次。").arg(m_errorText),
                                    tr("重新加载"), [this]() { requestRooms(); });
        }
        else
        {
            state = createStateCard(
                QStringLiteral("🏕"),
                tr("大厅里暂时没有房间"),
                tr("开好房后，在实例助手的联机页点「发布到联机大厅」，房间就会出现在这里。"),
                tr("去实例助手开房"), [this]() { emit openInstanceAssistantRequested(); });
        }

        m_listLayout->addStretch(1);
        auto *centerRow = new QHBoxLayout();
        centerRow->addStretch(1);
        centerRow->addWidget(state);
        centerRow->addStretch(1);
        m_listLayout->addLayout(centerRow);
        m_listLayout->addStretch(1);

        if (m_loading && !m_loadedOnce)
            setStatus(tr("加载中…"));
        else if (!m_errorText.isEmpty())
            setStatus(m_errorText, true);
        else
            setStatus(tr("共 0 个房间"));

        updateHeroMeta();
        return;
    }

    if (m_viewMode == static_cast<int>(ContentViewSwitch::List))
    {
        m_listLayout = new QVBoxLayout(m_container);
        m_listLayout->setContentsMargins(0, 0, 2, 8);
        m_listLayout->setSpacing(10);
        for (const ForumLobbyRoom &room : m_rooms)
        {
            QWidget *card = createRoomListCard(room);
            m_roomCards.append(card);
            m_listLayout->addWidget(card);
        }
        m_listLayout->addStretch(1);
        setStatus(m_total > static_cast<int>(m_rooms.size())
                      ? tr("显示 %1 个 · 在线 %2 个").arg(m_rooms.size()).arg(m_total)
                      : tr("共 %1 个房间").arg(m_rooms.size()));
    }
    else
    {
        m_masonryLayout = new MasonryLayout(m_container, 0, 10, 10);
        m_masonryLayout->setContentsMargins(0, 0, 2, 8);
        for (const ForumLobbyRoom &room : m_rooms)
        {
            QWidget *card = createRoomMasonryCard(room);
            m_roomCards.append(card);
            m_masonryLayout->addWidget(card);
        }
        setStatus(m_total > static_cast<int>(m_rooms.size())
                      ? tr("显示 %1 个 · 在线 %2 个").arg(m_rooms.size()).arg(m_total)
                      : tr("共 %1 个房间").arg(m_rooms.size()));
    }

    updateHeroMeta();
}

void MultiplayerLobbyPage::applyRoomViewMode(int mode)
{
    if (mode == m_viewMode)
        return;
    m_viewMode = mode;
    ContentViewSwitch::savePersisted(QStringLiteral("multiplayer_lobby"),
                                     static_cast<ContentViewSwitch::ViewMode>(mode));
    renderRooms();
}

QWidget *MultiplayerLobbyPage::createStateCard(const QString &glyph, const QString &title,
                                               const QString &hint, const QString &buttonText,
                                               const std::function<void()> &onClick)
{
    auto *card = new QFrame(m_container);
    card->setObjectName(QStringLiteral("mlStateCard"));
    card->setMaximumWidth(520);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(28, 26, 28, 26);
    layout->setSpacing(10);

    auto *glyphLabel = new QLabel(glyph, card);
    glyphLabel->setObjectName(QStringLiteral("mlStateGlyph"));
    glyphLabel->setAlignment(Qt::AlignHCenter);
    layout->addWidget(glyphLabel);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("mlStateTitle"));
    titleLabel->setAlignment(Qt::AlignHCenter);
    titleLabel->setWordWrap(true);
    titleLabel->setMaximumWidth(380);
    layout->addWidget(titleLabel);

    auto *hintLabel = new QLabel(hint, card);
    hintLabel->setObjectName(QStringLiteral("mlStateHint"));
    hintLabel->setAlignment(Qt::AlignHCenter);
    hintLabel->setWordWrap(true);
    hintLabel->setMaximumWidth(380);
    layout->addWidget(hintLabel);

    if (!buttonText.isEmpty() && onClick)
    {
        auto *button = new QPushButton(buttonText, card);
        button->setObjectName(QStringLiteral("mlPrimaryBtn"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(34);
        button->setMinimumWidth(140);
        connect(button, &QPushButton::clicked, this, onClick);

        auto *buttonRow = new QHBoxLayout();
        buttonRow->addStretch(1);
        buttonRow->addWidget(button);
        buttonRow->addStretch(1);
        layout->addSpacing(4);
        layout->addLayout(buttonRow);
    }

    return card;
}

QWidget *MultiplayerLobbyPage::createRoomListCard(const ForumLobbyRoom &room)
{
    auto *card = new LobbyListCard(m_container);
    card->setObjectName(QStringLiteral("modCardListItem"));
    card->setProperty("cardRole", "container");
    card->setFixedHeight(84);
    card->setToolTip(roomTooltip(room));
    card->onClick = [this, room]() { openRoomDetail(room); };

    auto *mainLayout = new QHBoxLayout(card);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    auto *iconLabel = new QLabel(card);
    iconLabel->setObjectName(QStringLiteral("modCardIcon"));
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setText(coreGlyph(room));
    iconLabel->setToolTip(room.coreLabel.isEmpty() ? ForumLobbyService::coreDisplayName(room.core)
                                                   : room.coreLabel);
    mainLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    auto *info = new QWidget(card);
    auto *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(6);
    mainLayout->addWidget(info, 1);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);

    auto *title = new ListCardTitle(info);
    title->setObjectName(QStringLiteral("modCardName"));
    title->setMinimumWidth(80);
    title->setFullText(roomTitle(room));
    title->setToolTip(roomTitle(room));
    // 标题按自然宽度排版，状态胶囊紧跟在标题后面，右侧空位留给邀请码 / 操作
    titleRow->addWidget(title, 0);

    QLabel *mineChip = nullptr;
    if (room.mine)
    {
        mineChip = new QLabel(tr("我的房间"), info);
        mineChip->setObjectName(QStringLiteral("mlChipMine"));
        titleRow->addWidget(mineChip, 0, Qt::AlignVCenter);
    }

    const RoomState state = roomState(room);
    auto *stateLabel = new QLabel(state.text, info);
    stateLabel->setObjectName(state.objectName);
    titleRow->addWidget(stateLabel, 0, Qt::AlignVCenter);
    titleRow->addStretch(1);
    infoLayout->addLayout(titleRow);

    auto *chips = new QWidget(info);
    chips->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *chipRow = new QHBoxLayout(chips);
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(6);

    auto addChip = [&](const QString &text) {
        auto *chip = new QLabel(text, chips);
        chip->setObjectName(QStringLiteral("modChip"));
        chip->setProperty("cardRole", "chip");
        chipRow->addWidget(chip);
    };
    if (!room.gameVersion.isEmpty())
        addChip(room.gameVersion);
    if (!room.loader.isEmpty())
        addChip(room.loader);
    addChip(room.coreLabel.isEmpty() ? ForumLobbyService::coreDisplayName(room.core)
                                     : room.coreLabel);
    if (room.joinCount > 0)
        addChip(tr("%1 次加入").arg(room.joinCount));
    chipRow->addStretch(1);
    infoLayout->addWidget(chips);

    // 右侧：邀请码（窄屏收起）+ 复制 + 加入
    const QString target = room.joinTarget().trimmed();
    auto *codeLabel = new QLabel(elide(target, 18), card);
    codeLabel->setObjectName(QStringLiteral("mlCardCode"));
    if (target.isEmpty())
    {
        codeLabel->setText(tr("未填邀请码"));
        codeLabel->setProperty("empty", true);
    }
    codeLabel->setToolTip(target.isEmpty() ? tr("房主没有填写邀请码 / 地址") : target);
    codeLabel->setMaximumWidth(150);
    codeLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    mainLayout->addWidget(codeLabel, 0, Qt::AlignVCenter);

    auto *copyBtn = new QPushButton(card);
    copyBtn->setObjectName(QStringLiteral("modCardActionBtn"));
    copyBtn->setProperty("cardRole", "actionBtn");
    copyBtn->setFixedSize(32, 32);
    copyBtn->setToolTip(tr("复制邀请码 / 地址"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setIcon(IconHelper::loadColoredIcon(
        QStringLiteral(":/Images/Icons/copy.svg"),
        QColor(ThemeManager::instance()->currentThemeColor()), 18));
    copyBtn->setIconSize(QSize(18, 18));
    copyBtn->setEnabled(!target.isEmpty());
    connect(copyBtn, &QPushButton::clicked, this, [this, target]() {
        if (target.isEmpty())
            return;
        QApplication::clipboard()->setText(target);
        setStatus(tr("已复制：%1").arg(elide(target, 32)));
        setJoinHint(tr("已复制：%1").arg(elide(target, 32)));
    });
    mainLayout->addWidget(copyBtn, 0, Qt::AlignVCenter);

    auto *joinBtn = new QPushButton(room.isOnline() ? tr("加入") : tr("已下架"), card);
    joinBtn->setObjectName(QStringLiteral("mlJoinBtn"));
    joinBtn->setCursor(Qt::PointingHandCursor);
    joinBtn->setFixedHeight(32);
    joinBtn->setMinimumWidth(72);
    joinBtn->setEnabled(room.isOnline());
    if (!room.isOnline())
        joinBtn->setToolTip(tr("这个房间已经下架，刷新后再看看别的吧"));
    else
    {
        QString missing;
        joinBtn->setToolTip(matchText(room, &missing));
    }
    connect(joinBtn, &QPushButton::clicked, this, [this, room]() { joinRoom(room); });
    mainLayout->addWidget(joinBtn, 0, Qt::AlignVCenter);

    card->setCollapsibleExtras(codeLabel, copyBtn, chips, mineChip);
    return card;
}

QWidget *MultiplayerLobbyPage::createRoomMasonryCard(const ForumLobbyRoom &room)
{
    auto *card = new LobbyClickCard(m_container);
    card->setObjectName(QStringLiteral("contentCard"));
    card->setFixedWidth(260);
    card->setToolTip(roomTooltip(room));
    card->onClick = [this, room]() { openRoomDetail(room); };
    MasonryContentCard::applyShadow(card);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(1, 1, 1, 1);
    cardLayout->setSpacing(0);

    constexpr int kBannerW = 258;
    constexpr int kBannerH = 120;

    auto *banner = new QLabel(card);
    banner->setObjectName(QStringLiteral("contentCardBanner"));
    banner->setFixedHeight(kBannerH);
    banner->setPixmap(roomBannerPixmap(room, kBannerW, kBannerH));
    banner->setProperty("gradientBanner", true);
    cardLayout->addWidget(banner);

    auto *logo = new QLabel(banner);
    logo->setObjectName(QStringLiteral("contentCardLogo"));
    logo->setAlignment(Qt::AlignCenter);
    logo->setPixmap(roomLogoPixmap(room, MasonryContentCard::logoSizeFor(true, kBannerH)));
    MasonryContentCard::placeLogo(logo, true, kBannerW, kBannerH);
    auto *logoShadow = new QGraphicsDropShadowEffect(logo);
    logoShadow->setBlurRadius(4);
    logoShadow->setOffset(0, 1);
    logoShadow->setColor(QColor(0, 0, 0, 180));
    logo->setGraphicsEffect(logoShadow);
    logo->raise();
    loadRoomCoverInto(banner, logo, room.cover, kBannerW, kBannerH);

    auto *body = new QWidget(card);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 14, 12, 12);
    bodyLayout->setSpacing(6);

    auto *nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(6);
    auto *name = new ForumKit::ElidedLabel(body);
    name->setObjectName(QStringLiteral("modCardName"));
    name->setMinimumWidth(80);
    name->setFullText(roomTitle(room));
    name->setToolTip(roomTitle(room));
    nameRow->addWidget(name, 1);

    const RoomState state = roomState(room);
    auto *stateLabel = new QLabel(state.text, body);
    stateLabel->setObjectName(state.objectName);
    nameRow->addWidget(stateLabel, 0, Qt::AlignVCenter);
    bodyLayout->addLayout(nameRow);

    const QString desc = room.description.trimmed();
    auto *descLabel = new QLabel(desc.isEmpty() ? tr("房主还没写简介") : elide(desc, 72), body);
    descLabel->setObjectName(QStringLiteral("contentCardDesc"));
    descLabel->setWordWrap(true);
    bodyLayout->addWidget(descLabel);

    QStringList metaParts;
    if (room.mine)
        metaParts << tr("我的房间");
    const QString host = hostDisplay(room);
    if (!host.isEmpty())
        metaParts << tr("房主 %1").arg(host);
    const QString published = ForumKit::relativeTime(room.createdAt);
    if (!published.isEmpty())
        metaParts << tr("%1发布").arg(published);
    if (!metaParts.isEmpty())
    {
        auto *metaLabel = new QLabel(metaParts.join(QStringLiteral(" · ")), body);
        metaLabel->setObjectName(QStringLiteral("mlMasonryMeta"));
        metaLabel->setWordWrap(true);
        bodyLayout->addWidget(metaLabel);
    }

    auto *chips = new QWidget(body);
    auto *chipRow = new QHBoxLayout(chips);
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(6);
    auto addChip = [&](const QString &text) {
        auto *chip = new QLabel(text, chips);
        chip->setObjectName(QStringLiteral("contentChip"));
        chipRow->addWidget(chip);
    };
    if (!room.gameVersion.isEmpty())
        addChip(room.gameVersion);
    if (!room.loader.isEmpty())
        addChip(room.loader);
    addChip(room.coreLabel.isEmpty() ? ForumLobbyService::coreDisplayName(room.core)
                                     : room.coreLabel);
    chipRow->addStretch(1);
    bodyLayout->addWidget(chips);

    // 操作：复制邀请码 / 一键加入（图标按钮沿用瀑布流卡片样式）
    const QString target = room.joinTarget().trimmed();
    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 4, 0, 0);
    actionRow->setSpacing(4);

    auto makeActionButton = [&](const QString &iconPath, const QString &tip) {
        auto *button = new QPushButton(body);
        button->setObjectName(QStringLiteral("contentCardActionBtn"));
        button->setFixedSize(32, 32);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setIcon(IconHelper::loadColoredIcon(
            iconPath, QColor(ThemeManager::instance()->currentThemeColor()), 18));
        button->setIconSize(QSize(18, 18));
        actionRow->addWidget(button);
        return button;
    };

    auto *copyBtn = makeActionButton(QStringLiteral(":/Images/Icons/copy.svg"),
                                     tr("复制邀请码 / 地址"));
    copyBtn->setEnabled(!target.isEmpty());
    connect(copyBtn, &QPushButton::clicked, this, [this, target]() {
        if (target.isEmpty())
            return;
        QApplication::clipboard()->setText(target);
        setStatus(tr("已复制：%1").arg(elide(target, 32)));
        setJoinHint(tr("已复制：%1").arg(elide(target, 32)));
    });

    auto *joinBtn = makeActionButton(QStringLiteral(":/Images/Icons/play.svg"), tr("一键加入"));
    joinBtn->setEnabled(room.isOnline());
    if (!room.isOnline())
        joinBtn->setToolTip(tr("这个房间已经下架，刷新后再看看别的吧"));
    connect(joinBtn, &QPushButton::clicked, this, [this, room]() { joinRoom(room); });

    actionRow->addStretch(1);
    bodyLayout->addLayout(actionRow);

    cardLayout->addWidget(body);
    return card;
}

void MultiplayerLobbyPage::openRoomDetail(const ForumLobbyRoom &room)
{
    if (!m_detailOverlay)
    {
        m_detailOverlay = new QWidget(this);
        m_detailOverlay->setObjectName(QStringLiteral("mlDetailOverlay"));
        m_detailOverlay->setAttribute(Qt::WA_StyledBackground, true);

        auto *layout = new QVBoxLayout(m_detailOverlay);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_detailPage = new MultiplayerLobbyDetailPage(m_detailOverlay);
        layout->addWidget(m_detailPage, 1);

        connect(m_detailPage, &MultiplayerLobbyDetailPage::backRequested,
                this, &MultiplayerLobbyPage::hideRoomDetail);
        connect(m_detailPage, &MultiplayerLobbyDetailPage::joinRequested,
                this, &MultiplayerLobbyPage::joinRoom);
        // 详情页评论区点「登录 / 注册」：转给社区页打开登录，回来还能接着评论
        connect(m_detailPage, &MultiplayerLobbyDetailPage::loginRequested,
                this, &MultiplayerLobbyPage::forumLoginRequested);
    }

    m_detailRoomId = room.id;
    m_detailPage->setRoom(room);
    m_detailPage->setActionHint(QString());
    m_detailPage->scrollToTop();

    // 详情页铺满本页：先收起大厅标题 / 筛选 / 列表，页面底色透出软件背景
    if (m_hero)
        m_hero->hide();
    if (m_filterBar)
        m_filterBar->hide();
    if (m_scroll)
        m_scroll->hide();

    m_detailOverlay->setGeometry(rect());
    m_detailOverlay->show();
    m_detailOverlay->raise();
}

void MultiplayerLobbyPage::hideRoomDetail()
{
    if (m_detailOverlay)
        m_detailOverlay->hide();
    m_detailRoomId = 0;

    if (m_hero)
        m_hero->show();
    if (m_filterBar)
        m_filterBar->show();
    if (m_scroll)
        m_scroll->show();
}

bool MultiplayerLobbyPage::closeDetailIfVisible()
{
    if (!m_detailOverlay || !m_detailOverlay->isVisible())
        return false;
    hideRoomDetail();
    return true;
}

bool MultiplayerLobbyPage::isDetailVisible() const
{
    return m_detailOverlay && m_detailOverlay->isVisible();
}

void MultiplayerLobbyPage::setJoinHint(const QString &text, bool error)
{
    if (isDetailVisible() && m_detailPage)
        m_detailPage->setActionHint(text, error);
}

void MultiplayerLobbyPage::syncDetailRoom()
{
    if (!isDetailVisible() || !m_detailPage)
        return;

    for (const ForumLobbyRoom &room : m_rooms)
    {
        if (room.id == m_detailRoomId)
        {
            m_detailPage->setRoom(room);
            return;
        }
    }

    // 列表里已经没有这个房间（房主下架 / 过期）：详情页按「已下架」展示
    ForumLobbyRoom stale = m_detailPage->room();
    stale.expiresIn = 0;
    m_detailPage->setRoom(stale);
}

QString MultiplayerLobbyPage::matchText(const ForumLobbyRoom &room, QString *missingText) const
{
    const LobbyInstanceMatcher::Requirement requirement = LobbyInstanceMatcher::requirementFor(
        room.gameVersion, room.loader, room.requiredMods(), room.auxMods, room.resourcePacks,
        room.shaderPacks);
    const LobbyInstanceMatcher::JoinPlan plan =
        LobbyInstanceMatcher::makePlan(requirement, m_instancePool);

    if (missingText)
    {
        QStringList parts;
        if (!plan.best.missingMods.isEmpty())
            parts << tr("缺模组：%1").arg(plan.best.missingMods.join(QStringLiteral("、")));
        if (!plan.best.missingAuxMods.isEmpty())
            parts << tr("缺辅助模组 %1 个").arg(plan.best.missingAuxMods.size());
        if (!plan.best.missingResourcePacks.isEmpty())
            parts << tr("缺资源包 %1 个").arg(plan.best.missingResourcePacks.size());
        if (!plan.best.missingShaderPacks.isEmpty())
            parts << tr("缺光影包 %1 个").arg(plan.best.missingShaderPacks.size());
        *missingText = parts.isEmpty() ? QString() : parts.join(QStringLiteral("；"));
    }

    if (!plan.found)
        return plan.installHint.trimmed().isEmpty()
                   ? (room.gameVersion.isEmpty() ? tr("任意版本都能加入")
                                                 : tr("本机没有 %1 的实例，需要先装一个")
                                                       .arg(room.gameVersion))
                   : plan.installHint;

    const QString instanceName = plan.best.instance.id;
    if (plan.best.level == LobbyInstanceMatcher::MatchLevel::Mods)
        return tr("一键用「%1」加入 · 必装模组齐全").arg(instanceName);
    if (plan.best.level == LobbyInstanceMatcher::MatchLevel::Loader)
        return tr("一键用「%1」加入 · 缺 %2 个模组").arg(instanceName)
            .arg(plan.best.missingMods.size());
    return tr("一键用「%1」加入 · 仅版本匹配").arg(instanceName);
}

void MultiplayerLobbyPage::joinRoom(const ForumLobbyRoom &room)
{
    // 加入过程中的问题一律就地提示 + 左下角通知，不再弹窗口把详情页挡住
    if (!room.isOnline())
    {
        setJoinHint(tr("这个房间已经下架，刷新后再看看别的吧。"), true);
        return;
    }

    // 房间标了前置整合包：只提醒一句，不拦路（详情页里有下载入口）
    if (room.hasModpack())
        showModpackNotice(room);

    if (room.isTerracotta())
    {
        const QString code = room.inviteCode.trimmed();
        if (code.isEmpty())
        {
            setJoinHint(tr("这个房间没有可用的邀请码，请让房主重新发布。"), true);
            return;
        }

        auto *terracotta = TerracottaClient::instance();
        if (terracotta->installedVersion().isEmpty())
        {
            setJoinHint(tr("还没安装陶瓦联机核心，需要先去实例助手安装。"), true);
            emit openInstanceAssistantRequested();
            return;
        }
        if (terracotta->isInRoom())
        {
            setJoinHint(tr("你已经在陶瓦联机的房间里了，先离开当前房间再加入。"), true);
            return;
        }

        m_pendingJoin = true;
        m_pendingWaitingCore = false;
        m_pendingRoomId = room.id;
        m_pendingCode = code;
        m_pendingRoom = room;
        ForumLobbyService::instance()->recordJoin(room.id, nullptr, nullptr);

        QString playerName = ForumAccountService::instance()->playerName();
        if (playerName.isEmpty())
            playerName = tr("BlockBox 玩家");

        if (terracotta->state() == TerracottaClient::State::Waiting)
        {
            if (!terracotta->joinRoom(code, playerName))
            {
                handlePendingJoin(false, tr("无法加入房间，请稍后再试。"));
                return;
            }
            setStatus(tr("正在连接房主…"));
            setJoinHint(tr("正在连接房主…"));
        }
        else
        {
            m_pendingWaitingCore = true;
            terracotta->start();
            setStatus(tr("正在启动陶瓦联机核心，启动后会自动加入…"));
            setJoinHint(tr("正在启动陶瓦联机核心，启动后会自动加入…"));
        }
        return;
    }

    // 红石 / frp / 直连：地址本身就够用
    QString host;
    quint16 port = 0;
    if (!parseHostPort(room.address, &host, &port))
    {
        setJoinHint(tr("这个房间没有可用的连接地址，请让房主重新发布。"), true);
        return;
    }

    // 红石源房间不在论坛上，没有「加入热度」可记
    if (!room.isHongshiSource())
        ForumLobbyService::instance()->recordJoin(room.id, nullptr, nullptr);
    emit joinGameRequested(room, host, port);
}

void MultiplayerLobbyPage::showModpackNotice(const ForumLobbyRoom &room)
{
    // 整合包只在左下角提一句：详情页已经列了整合包信息和下载按钮，不用再弹窗挡路
    const QString name = room.modpackName.trimmed();
    if (name.isEmpty())
        return;
    NotificationManager::showInfo(
        this, tr("这个房间有前置整合包「%1」，建议先在房间详情里装好再加入。").arg(name));
}

void MultiplayerLobbyPage::onTerracottaStateChanged()
{
    if (!m_pendingJoin)
        return;

    auto *terracotta = TerracottaClient::instance();
    const auto state = terracotta->state();

    if (state == TerracottaClient::State::Exception
        || state == TerracottaClient::State::Fatal)
    {
        handlePendingJoin(false, tr("连不上房主，请稍后再试。"));
        return;
    }

    if (m_pendingWaitingCore && state == TerracottaClient::State::Waiting)
    {
        m_pendingWaitingCore = false;
        QString playerName = ForumAccountService::instance()->playerName();
        if (playerName.isEmpty())
            playerName = tr("BlockBox 玩家");

        if (!terracotta->joinRoom(m_pendingCode, playerName))
        {
            handlePendingJoin(false, tr("无法加入房间，请稍后再试。"));
            return;
        }
        setStatus(tr("正在连接房主…"));
        setJoinHint(tr("正在连接房主…"));
        return;
    }

    if (state == TerracottaClient::State::GuestOk)
    {
        QString host;
        quint16 port = 0;
        if (!parseHostPort(terracotta->serverUrl(), &host, &port))
        {
            handlePendingJoin(false, tr("联机核心没有返回可用的服务器地址。"));
            return;
        }
        const ForumLobbyRoom room = m_pendingRoom;
        m_pendingJoin = false;
        m_pendingWaitingCore = false;
        setStatus(tr("连接成功，正在启动游戏…"));
        setJoinHint(tr("连接成功，正在启动游戏…"));
        emit joinGameRequested(room, host, port);
    }
}

void MultiplayerLobbyPage::handlePendingJoin(bool success, const QString &message)
{
    m_pendingJoin = false;
    m_pendingWaitingCore = false;
    m_pendingRoomId = 0;
    m_pendingCode.clear();
    m_pendingRoom = ForumLobbyRoom();

    if (!success && !message.isEmpty())
    {
        // 详情页里就地标红，另外左下角给一条通知；不开弹窗
        setStatus(message, true);
        setJoinHint(message, true);
        NotificationManager::showError(this, message);
    }
}

void MultiplayerLobbyPage::setStatus(const QString &text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);

    // 错误态走动态属性 + 样式表，避免内联写死颜色（换主题后仍可读）
    if (m_statusLabel->property("error").toBool() == error)
        return;
    m_statusLabel->setProperty("error", error);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

bool MultiplayerLobbyPage::parseHostPort(const QString &text, QString *host, quint16 *port)
{
    QString value = text.trimmed();
    if (value.isEmpty())
        return false;
    if (value.startsWith(QStringLiteral("http://")))
        value.remove(0, 7);
    else if (value.startsWith(QStringLiteral("https://")))
        value.remove(0, 8);
    while (value.endsWith(QLatin1Char('/')))
        value.chop(1);

    quint16 parsedPort = 25565;
    QString parsedHost = value;
    const int colon = value.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && value.indexOf(QLatin1Char(':')) == colon)
    {
        bool ok = false;
        const int candidate = value.mid(colon + 1).toInt(&ok);
        if (ok && candidate > 0 && candidate <= 65535)
        {
            parsedPort = quint16(candidate);
            parsedHost = value.left(colon);
        }
    }

    if (parsedHost.isEmpty())
        return false;
    if (host)
        *host = parsedHost;
    if (port)
        *port = parsedPort;
    return true;
}
