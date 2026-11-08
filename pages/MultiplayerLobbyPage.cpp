/**
 * @file   MultiplayerLobbyPage.cpp
 * @brief  联机大厅页面实现：横幅概况、筛选栏、房间卡与空 / 出错状态
 * @author BlockBox Team
 * @date   2026-11-06
 *
 * 样式全部通过 StyleKit::resolve 解析 @TOKEN@ 令牌，跟随主题色与亮 / 暗主题
 * 即时重算；房间卡在窄屏（手机 / 窄窗口）自动由左右排改成上下排。
 */
#include "MultiplayerLobbyPage.h"

#include "components/AppMessageBox.h"
#include "components/ForumKit.h"
#include "layouts/FlowLayout.h"
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
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
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

/**
 * @brief 房间封面缩略图（16:9）
 *
 * 房主传了封面就按 16:9 居中裁切画出来；没有封面时画一层主题色渐变，
 * 中间压一个核心类型单字（陶 / 红 / 穿…），卡片左侧不会空一块。
 * 圆角与描边自己画，跟随主题色和边框色。
 */
class LobbyCoverThumb : public QWidget
{
public:
    LobbyCoverThumb(const QString &coverPath, const QString &glyph, QWidget *parent)
        : QWidget(parent)
        , m_glyph(glyph)
    {
        setFixedSize(140, 79);
        setAttribute(Qt::WA_StyledBackground, false);

        const QString path = coverPath.trimmed();
        if (path.isEmpty())
            return;
        const QString url = ForumAccountService::instance()->absoluteUrl(path);
        if (url.isEmpty())
            return;

        QPointer<LobbyCoverThumb> guard(this);
        ForumImageStore::instance()->requestImage(url, [guard](const QImage &image) {
            if (guard.isNull() || image.isNull())
                return;
            guard->m_image = image;
            guard->update();
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = 10.0;
        QPainterPath clip;
        clip.addRoundedRect(box, radius, radius);
        painter.setClipPath(clip);

        if (!m_image.isNull())
        {
            const QImage scaled = m_image.scaled(size(), Qt::KeepAspectRatioByExpanding,
                                                 Qt::SmoothTransformation);
            painter.drawImage(QPoint((width() - scaled.width()) / 2,
                                     (height() - scaled.height()) / 2),
                              scaled);
        }
        else
        {
            const QColor base(ThemeManager::instance()->currentThemeColor());
            QLinearGradient gradient(0, 0, width(), height());
            gradient.setColorAt(0.0, base.lighter(155));
            gradient.setColorAt(1.0, base.darker(125));
            painter.fillRect(rect(), gradient);

            QFont glyphFont = font();
            glyphFont.setPixelSize(26);
            glyphFont.setBold(true);
            painter.setFont(glyphFont);
            painter.setPen(QColor(255, 255, 255, 235));
            painter.drawText(rect(), Qt::AlignCenter, m_glyph);
        }

        painter.setClipping(false);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(ThemeManager::instance()->currentBorderColor()), 1));
        painter.drawRoundedRect(box, radius, radius);
    }

private:
    QString m_glyph;
    QImage m_image;
};

/** 核心类型 → 封面占位上的单字徽记 */
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

/**
 * @brief 房间卡标题：跟着可用宽度省略，且不会把整张卡撑宽
 *
 * QLabel 的最小宽度默认等于整行文字宽度，长房间名会把卡片顶到放不下；
 * 这里把最小宽度压到 120px，配合 ElidedLabel 的重新省略，窄屏也能正常截断。
 */
class CardTitleLabel : public ForumKit::ElidedLabel
{
public:
    explicit CardTitleLabel(QWidget *parent)
        : ForumKit::ElidedLabel(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    // 省略后的文字会把 sizeHint 越算越窄，这里按完整标题给出稳定的期望宽度
    QSize sizeHint() const override
    {
        const QFontMetrics metrics(fontMetrics());
        const int width = qBound(120, metrics.horizontalAdvance(fullText()) + 2, 420);
        return QSize(width, QLabel::sizeHint().height());
    }

    QSize minimumSizeHint() const override
    {
        return QSize(120, QLabel::minimumSizeHint().height());
    }
};

/**
 * @brief 房间卡：宽屏左封面右操作，窄屏（手机 / 窄窗口）自动改成上下排
 *
 * 卡片宽过 640 时是「封面 | 内容 | 邀请码+加入」一行；再窄就把整行竖过来，
 * 否则右侧的邀请码和按钮会把标题挤出卡片外。切换只改布局方向，控件本身
 * 一直复用，拖动窗口边缘时不会闪。
 */
class LobbyRoomCard : public QFrame
{
public:
    LobbyRoomCard(QWidget *parent, const QString &coverPath, const QString &glyph)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("mlRoomCard"));

        m_root = new QBoxLayout(QBoxLayout::LeftToRight, this);
        m_root->setContentsMargins(14, 14, 14, 14);
        m_root->setSpacing(14);

        m_cover = new LobbyCoverThumb(coverPath, glyph, this);
        m_root->addWidget(m_cover, 0, Qt::AlignTop);

        m_body = new QWidget(this);
        m_bodyLayout = new QVBoxLayout(m_body);
        m_bodyLayout->setContentsMargins(0, 0, 0, 0);
        m_bodyLayout->setSpacing(5);
        m_root->addWidget(m_body, 1);

        m_side = new QWidget(this);
        m_sideLayout = new QVBoxLayout(m_side);
        m_sideLayout->setContentsMargins(0, 0, 0, 0);
        m_sideLayout->setSpacing(8);
        m_root->addWidget(m_side, 0);
        m_root->setAlignment(m_side, Qt::AlignRight | Qt::AlignVCenter);
    }

    QVBoxLayout *bodyLayout() const { return m_bodyLayout; }
    QVBoxLayout *sideLayout() const { return m_sideLayout; }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        updateMode();
    }

    void showEvent(QShowEvent *event) override
    {
        QFrame::showEvent(event);
        updateMode();
    }

private:
    void updateMode()
    {
        const bool compact = width() > 0 && width() < 640;
        const int mode = compact ? 1 : 0;
        if (mode == m_mode)
            return;
        m_mode = mode;

        if (compact)
        {
            m_root->setDirection(QBoxLayout::TopToBottom);
            m_root->setStretchFactor(m_body, 0);
            m_root->setAlignment(m_cover, Qt::AlignLeft);
            m_root->setAlignment(m_side, Qt::Alignment());
            // 极窄（手机竖屏）时省掉封面，把高度让给标题和按钮
            m_cover->setVisible(width() >= 360);
        }
        else
        {
            m_root->setDirection(QBoxLayout::LeftToRight);
            m_root->setStretchFactor(m_body, 1);
            m_root->setAlignment(m_cover, Qt::AlignTop);
            m_root->setAlignment(m_side, Qt::AlignRight | Qt::AlignVCenter);
            m_cover->setVisible(true);
        }
    }

    QBoxLayout *m_root = nullptr;
    QWidget *m_cover = nullptr;
    QWidget *m_body = nullptr;
    QWidget *m_side = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QVBoxLayout *m_sideLayout = nullptr;
    int m_mode = -1;
};

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
} // namespace

MultiplayerLobbyPage::MultiplayerLobbyPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("multiplayerLobbyPage"));
    setAttribute(Qt::WA_StyledBackground, true);
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
    m_roomsLayout = new QVBoxLayout(m_container);
    m_roomsLayout->setContentsMargins(0, 0, 2, 8);
    m_roomsLayout->setSpacing(10);
    m_roomsLayout->addStretch();
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
        /* 页面底 */
        "QWidget#multiplayerLobbyPage { background: @BG_BASE@; }"

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
        "QComboBox#mlCoreCombo {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 9px;"
        "  color: @TEXT_PRIMARY@; font-size: 13px; padding: 0 30px 0 12px;"
        "}"
        "QComboBox#mlCoreCombo:hover { border-color: @BORDER_HOVER@; }"
        "QComboBox#mlCoreCombo:focus { border: 1px solid @PRIMARY@; }"
        "QComboBox#mlCoreCombo::drop-down {"
        "  subcontrol-origin: padding; subcontrol-position: top right;"
        "  width: 26px; border: none; border-left: 1px solid @BORDER_LIGHT@;"
        "  border-top-right-radius: 9px; border-bottom-right-radius: 9px;"
        "  background: transparent;"
        "}"
        "QComboBox#mlCoreCombo::drop-down:hover { background: @BG_HOVER@; }"
        "QComboBox#mlCoreCombo::down-arrow {"
        "  image: url(:/Images/Icons/arrow_down_triangle.svg);"
        "  width: 10px; height: 10px; margin-right: 8px; border: none;"
        "}"
        "QComboBox#mlCoreCombo QAbstractItemView {"
        "  background: @BG_CARD@; color: @TEXT_PRIMARY@; border: 1px solid @BORDER_STRONG@;"
        "  border-radius: 10px; padding: 4px; outline: none;"
        "  selection-background-color: @PRIMARY_BG@; selection-color: @TEXT_ACCENT@;"
        "}"
        "QLabel#mlStatus {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#mlStatus[error=\"true\"] { color: @DANGER_TEXT@; }"

        /* 房间卡 */
        "QFrame#mlRoomCard {"
        "  background: @BG_CARD@; border: 1px solid @BORDER@; border-radius: 14px;"
        "}"
        "QFrame#mlRoomCard:hover { border-color: @PRIMARY@; }"
        "QLabel#mlCardTitle {"
        "  color: @TEXT_PRIMARY@; font-size: 15px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlCardMeta {"
        "  color: @TEXT_SECONDARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#mlCardDesc {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLabel#mlCardMatch {"
        "  color: @TEXT_ACCENT@; font-size: 11px; font-weight: 600;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlChip {"
        "  color: @TEXT_SECONDARY@; background: @BG_MUTED@; border: 1px solid @BORDER@;"
        "  border-radius: 9px; padding: 1px 8px; font-size: 11px; font-weight: 600;"
        "}"
        "QLabel#mlChipMine {"
        "  color: @TEXT_ACCENT@; background: @PRIMARY_RGBA_15@;"
        "  border: 1px solid @PRIMARY_RGBA_38@; border-radius: 9px; padding: 1px 8px;"
        "  font-size: 11px; font-weight: 700;"
        "}"
        "QLabel#mlChipOnline {"
        "  color: @SUCCESS@; font-size: 11px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlChipPlaying {"
        "  color: @WARNING@; font-size: 11px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QLabel#mlChipFull {"
        "  color: @TEXT_TERTIARY@; font-size: 11px; font-weight: 700;"
        "  background: transparent; border: none;"
        "}"
        "QFrame#mlCodeBox {"
        "  background: @BG_MUTED@; border: 1px dashed @BORDER_DASHED@; border-radius: 9px;"
        "}"
        "QLabel#mlCode {"
        "  color: @TEXT_PRIMARY@; font-family: Consolas, \"Microsoft YaHei\";"
        "  font-size: 13px; font-weight: 700; background: transparent; border: none;"
        "}"
        "QLabel#mlCodeEmpty {"
        "  color: @TEXT_TERTIARY@; font-size: 12px; background: transparent; border: none;"
        "}"
        "QPushButton#mlJoinBtn {"
        "  background: @PRIMARY@; color: @TEXT_ON_PRIMARY@; border: none; border-radius: 9px;"
        "  padding: 0 16px; font-size: 13px; font-weight: 700;"
        "}"
        "QPushButton#mlJoinBtn:hover { background: @PRIMARY_HOVER@; }"
        "QPushButton#mlJoinBtn:pressed { background: @PRIMARY_PRESSED@; }"
        "QPushButton#mlJoinBtn:disabled { background: @BG_DISABLED@; color: @TEXT_DISABLED@; }"
        "QPushButton#mlCopyBtn {"
        "  background: transparent; color: @TEXT_SECONDARY@; border: none; border-radius: 6px;"
        "  padding: 0 8px; font-size: 11px; font-weight: 600;"
        "}"
        "QPushButton#mlCopyBtn:hover { color: @PRIMARY@; background: @PRIMARY_RGBA_10@; }"
        "QPushButton#mlCopyBtn:pressed { background: @PRIMARY_RGBA_15@; }"
        "QPushButton#mlCopyBtn:disabled { color: @TEXT_DISABLED@; }"

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

        /* 列表容器 */
        "QScrollArea#mlScroll, QWidget#mlContainer { background: transparent; border: none; }"
    )));
}

void MultiplayerLobbyPage::refresh()
{
    requestRooms();
}

void MultiplayerLobbyPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void MultiplayerLobbyPage::updateResponsiveLayout()
{
    if (!m_rootLayout || !m_heroLayout || !m_heroActions)
        return;

    // 手机 / 窄窗口：横幅改成上下排、页边距收窄，房间卡由卡片自己切换
    const bool compact = width() > 0 && width() < 720;
    if (compact == m_compactLayout)
        return;
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

    const QString keyword = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    const QString core = m_coreCombo ? m_coreCombo->currentData().toString() : QString();

    ForumLobbyService::instance()->fetchRooms(
        keyword, core,
        [this](const QVector<ForumLobbyRoom> &rooms, int total) {
            m_loading = false;
            m_loadedOnce = true;
            m_errorText.clear();
            m_rooms = rooms;
            m_total = total;
            // renderRooms 里会顺带把「共 N 个房间」写进状态栏
            renderRooms();
        },
        [this](const QString &message, int) {
            m_loading = false;
            m_loadedOnce = true;
            m_errorText = message;
            setStatus(tr("刷新失败：%1").arg(message), true);
            // 一个房间都没拿到时把列表换成「连不上」的占位卡，已有列表则原样保留
            if (m_rooms.isEmpty())
                renderRooms();
            else
                updateHeroMeta();
        });
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

void MultiplayerLobbyPage::renderRooms()
{
    if (!m_roomsLayout)
        return;

    while (m_roomsLayout->count() > 0)
    {
        QLayoutItem *item = m_roomsLayout->takeAt(0);
        if (item->widget())
        {
            // 摘出布局后控件还留在容器上，先隐藏再排队删除，否则新内容会和旧卡叠一帧
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        else if (item->layout())
        {
            for (int i = 0; i < item->layout()->count(); ++i)
            {
                if (QWidget *widget = item->layout()->itemAt(i)->widget())
                    widget->hide();
            }
            ForumKit::clearLayout(item->layout());
        }
        delete item;
    }

    if (m_rooms.isEmpty())
    {
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

        m_roomsLayout->addStretch(1);
        auto *centerRow = new QHBoxLayout();
        centerRow->addStretch(1);
        centerRow->addWidget(state);
        centerRow->addStretch(1);
        m_roomsLayout->addLayout(centerRow);
        m_roomsLayout->addStretch(1);

        if (m_loading && !m_loadedOnce)
            setStatus(tr("加载中…"));
        else if (!m_errorText.isEmpty())
            setStatus(tr("刷新失败：%1").arg(m_errorText), true);
        else
            setStatus(tr("共 0 个房间"));

        updateHeroMeta();
        return;
    }

    for (const ForumLobbyRoom &room : m_rooms)
        m_roomsLayout->addWidget(createRoomCard(room));
    m_roomsLayout->addStretch();

    if (m_total > static_cast<int>(m_rooms.size()))
        setStatus(tr("显示 %1 个 · 在线 %2 个").arg(m_rooms.size()).arg(m_total));
    else
        setStatus(tr("共 %1 个房间").arg(m_rooms.size()));

    updateHeroMeta();
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

QWidget *MultiplayerLobbyPage::createRoomCard(const ForumLobbyRoom &room)
{
    // 宽屏左封面右操作、窄屏上下排，由卡片自己按宽度切换
    auto *card = new LobbyRoomCard(m_container, room.cover, coreGlyph(room));
    QVBoxLayout *body = card->bodyLayout();

    // 标题行：房间名 + 「我的房间」+ 在线状态
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);

    const QString roomTitle = room.title.trimmed().isEmpty() ? tr("未命名房间") : room.title.trimmed();
    auto *title = new CardTitleLabel(card);
    title->setObjectName(QStringLiteral("mlCardTitle"));
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    title->setFullText(roomTitle);
    titleRow->addWidget(title, 0);

    if (room.mine)
    {
        auto *mine = new QLabel(tr("我的房间"), card);
        mine->setObjectName(QStringLiteral("mlChipMine"));
        titleRow->addWidget(mine);
    }

    const RoomState state = roomState(room);
    auto *stateLabel = new QLabel(state.text, card);
    stateLabel->setObjectName(state.objectName);
    titleRow->addWidget(stateLabel);

    titleRow->addStretch(1);
    body->addLayout(titleRow);

    // 元信息：版本 / 加载器 / 房主 / 人数 / 热度 / 发布时间
    QStringList meta;
    if (!room.gameVersion.isEmpty())
        meta << tr("MC %1").arg(room.gameVersion);
    if (!room.loader.isEmpty())
        meta << room.loader;
    const QString hostName = room.host.displayName.trimmed().isEmpty()
                                 ? room.host.username.trimmed()
                                 : room.host.displayName.trimmed();
    if (!hostName.isEmpty())
        meta << tr("房主 %1").arg(hostName);
    meta << tr("%1 人上限").arg(room.maxPlayers);
    if (room.joinCount > 0)
        meta << tr("%1 次加入").arg(room.joinCount);
    const QString publishTime = ForumKit::relativeTime(room.createdAt);
    if (!publishTime.isEmpty())
        meta << tr("%1发布").arg(publishTime);

    auto *metaLabel = new QLabel(meta.join(QStringLiteral("  ·  ")), card);
    metaLabel->setObjectName(QStringLiteral("mlCardMeta"));
    metaLabel->setWordWrap(true);
    body->addWidget(metaLabel);

    if (!room.description.trimmed().isEmpty())
    {
        auto *desc = new QLabel(elide(room.description.trimmed(), 96), card);
        desc->setObjectName(QStringLiteral("mlCardDesc"));
        desc->setWordWrap(true);
        body->addWidget(desc);
    }

    // 底行：核心 / 前置整合包 / 本机实例匹配情况（放不下自动换行）
    auto *chipHost = new QWidget(card);
    auto *chipRow = new FlowLayout(chipHost, 0, 6, 4);

    auto *coreChip = new QLabel(room.coreLabel.isEmpty()
                                    ? ForumLobbyService::coreDisplayName(room.core)
                                    : room.coreLabel,
                                chipHost);
    coreChip->setObjectName(QStringLiteral("mlChip"));
    chipRow->addWidget(coreChip);

    if (room.hasModpack())
    {
        auto *packChip = new QLabel(tr("前置整合包：%1").arg(elide(room.modpackName, 14)), chipHost);
        packChip->setObjectName(QStringLiteral("mlChip"));
        packChip->setToolTip(room.modpackUrl.isEmpty()
                                 ? tr("加入前可以先把这个整合包装上")
                                 : tr("加入前可以先把这个整合包装上：%1").arg(room.modpackUrl));
        chipRow->addWidget(packChip);
    }

    QString missing;
    auto *match = new QLabel(matchText(room, &missing), chipHost);
    match->setObjectName(QStringLiteral("mlCardMatch"));
    match->setToolTip(missing.isEmpty() ? match->text() : tr("缺少模组：%1").arg(missing));
    chipRow->addWidget(match);

    body->addWidget(chipHost);

    // 右侧：邀请码 / 地址 + 复制 + 一键加入
    QVBoxLayout *side = card->sideLayout();

    auto *codeBox = new QFrame(card);
    codeBox->setObjectName(QStringLiteral("mlCodeBox"));
    auto *codeLayout = new QHBoxLayout(codeBox);
    codeLayout->setContentsMargins(10, 4, 6, 4);
    codeLayout->setSpacing(4);

    const QString target = room.joinTarget().trimmed();
    auto *code = new QLabel(target.isEmpty() ? tr("未填写邀请码") : elide(target, 20), codeBox);
    code->setObjectName(target.isEmpty() ? QStringLiteral("mlCodeEmpty") : QStringLiteral("mlCode"));
    code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    code->setToolTip(target.isEmpty() ? tr("房主没有填写邀请码 / 地址") : target);
    codeLayout->addWidget(code, 1);

    auto *copyBtn = new QPushButton(tr("复制"), codeBox);
    copyBtn->setObjectName(QStringLiteral("mlCopyBtn"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setFixedHeight(26);
    copyBtn->setEnabled(!target.isEmpty());
    connect(copyBtn, &QPushButton::clicked, this, [this, target]() {
        if (target.isEmpty())
            return;
        QApplication::clipboard()->setText(target);
        setStatus(tr("已复制：%1").arg(elide(target, 32)));
    });
    codeLayout->addWidget(copyBtn);

    side->addWidget(codeBox);

    auto *joinBtn = new QPushButton(room.isOnline() ? tr("一键加入") : tr("已下架"), card);
    joinBtn->setObjectName(QStringLiteral("mlJoinBtn"));
    joinBtn->setCursor(Qt::PointingHandCursor);
    joinBtn->setFixedHeight(34);
    joinBtn->setMinimumWidth(120);
    joinBtn->setEnabled(room.isOnline());
    if (!room.isOnline())
        joinBtn->setToolTip(tr("这个房间已经下架，刷新后再看看别的吧"));
    connect(joinBtn, &QPushButton::clicked, this, [this, room]() { joinRoom(room); });
    side->addWidget(joinBtn);

    return card;
}

QString MultiplayerLobbyPage::matchText(const ForumLobbyRoom &room, QString *missingText) const
{
    LobbyInstanceInfo best;
    LobbyInstanceMatcher::MatchLevel level = LobbyInstanceMatcher::MatchLevel::None;
    QStringList missing;
    const bool found = LobbyInstanceMatcher::pickBest(room.gameVersion, room.loader, room.mods,
                                                      &best, &level, &missing);
    if (missingText)
        *missingText = missing.join(QStringLiteral("、"));

    if (!found)
    {
        return room.gameVersion.isEmpty() ? tr("任意版本都能加入")
                                          : tr("本机没有 %1 的实例，需要先装一个").arg(room.gameVersion);
    }

    if (level == LobbyInstanceMatcher::MatchLevel::Mods)
        return tr("一键用「%1」加入 · 模组齐全").arg(best.id);
    if (level == LobbyInstanceMatcher::MatchLevel::Loader)
        return tr("一键用「%1」加入 · 版本和加载器匹配").arg(best.id);
    return tr("一键用「%1」加入 · 仅版本匹配").arg(best.id);
}

void MultiplayerLobbyPage::joinRoom(const ForumLobbyRoom &room)
{
    if (!room.isOnline())
    {
        AppMessageBox::information(this, tr("房间已过期"), tr("这个房间已经下架，刷新后再看看别的吧。"));
        return;
    }

    // 房间标了前置整合包：加入前先过一次提醒（在线的告诉玩家可以从这装，链接的给地址）
    if (room.hasModpack())
        showModpackNotice(room);

    if (room.isTerracotta())
    {
        const QString code = room.inviteCode.trimmed();
        if (code.isEmpty())
        {
            AppMessageBox::information(this, tr("没有邀请码"), tr("这个房间没有可用的邀请码，请让房主重新发布。"));
            return;
        }

        auto *terracotta = TerracottaClient::instance();
        if (terracotta->installedVersion().isEmpty())
        {
            AppMessageBox::information(
                this, tr("还没安装陶瓦联机核心"),
                tr("这是陶瓦联机的房间，需要先在实例助手的联机页安装陶瓦联机核心。"));
            emit openInstanceAssistantRequested();
            return;
        }
        if (terracotta->isInRoom())
        {
            AppMessageBox::information(this, tr("已在房间中"),
                                       tr("你已经在陶瓦联机的房间里了，先离开当前房间再加入其他房间。"));
            return;
        }

        m_pendingJoin = true;
        m_pendingWaitingCore = false;
        m_pendingRoomId = room.id;
        m_pendingCode = code;
        m_pendingVersion = room.gameVersion;
        m_pendingLoader = room.loader;
        m_pendingMods = room.mods;
        ForumLobbyService::instance()->recordJoin(room.id, nullptr, nullptr);

        QString playerName;
        if (ForumAccountService::instance()->isLoggedIn())
            playerName = ForumAccountService::instance()->user().displayName;
        if (playerName.trimmed().isEmpty())
            playerName = tr("BlockBox 玩家");

        if (terracotta->state() == TerracottaClient::State::Waiting)
        {
            if (!terracotta->joinRoom(code, playerName))
            {
                handlePendingJoin(false, tr("无法加入房间，请稍后再试。"));
                return;
            }
            setStatus(tr("正在连接房主…"));
        }
        else
        {
            m_pendingWaitingCore = true;
            terracotta->start();
            setStatus(tr("正在启动陶瓦联机核心，启动后会自动加入…"));
        }
        return;
    }

    // 红石 / frp / 直连：地址本身就够用
    QString host;
    quint16 port = 0;
    if (!parseHostPort(room.address, &host, &port))
    {
        AppMessageBox::information(this, tr("地址无效"),
                                   tr("这个房间没有可用的连接地址，请让房主重新发布。"));
        return;
    }

    ForumLobbyService::instance()->recordJoin(room.id, nullptr, nullptr);
    emit joinGameRequested(host, port, room.gameVersion, room.loader, room.mods);
}

void MultiplayerLobbyPage::showModpackNotice(const ForumLobbyRoom &room)
{
    if (room.modpackMode == QLatin1String("online") && !room.modpackUrl.isEmpty())
    {
        AppMessageBox::information(
            this, tr("需要前置整合包"),
            tr("房主给这个房间标了前置整合包「%1」。\n\n"
               "建议先在「资源 > 导入整合包」里下载安装它：\n%2\n\n装好再回来加入。")
                .arg(room.modpackName, room.modpackUrl));
    }
    else if (room.modpackMode == QLatin1String("link") && !room.modpackUrl.isEmpty())
    {
        AppMessageBox::information(
            this, tr("需要前置整合包"),
            tr("房主给这个房间标了前置整合包「%1」。\n\n下载地址：\n%2\n\n自己装好再回来加入。")
                .arg(room.modpackName, room.modpackUrl));
    }
    else
    {
        AppMessageBox::information(
            this, tr("需要前置整合包"),
            tr("房主给这个房间标了前置整合包「%1」，加入前先确认你装了它。")
                .arg(room.modpackName));
    }
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
        QString playerName;
        if (ForumAccountService::instance()->isLoggedIn())
            playerName = ForumAccountService::instance()->user().displayName;
        if (playerName.trimmed().isEmpty())
            playerName = tr("BlockBox 玩家");

        if (!terracotta->joinRoom(m_pendingCode, playerName))
        {
            handlePendingJoin(false, tr("无法加入房间，请稍后再试。"));
            return;
        }
        setStatus(tr("正在连接房主…"));
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
        const QString version = m_pendingVersion;
        const QString loader = m_pendingLoader;
        const QStringList mods = m_pendingMods;
        m_pendingJoin = false;
        m_pendingWaitingCore = false;
        setStatus(tr("连接成功，正在启动游戏…"));
        emit joinGameRequested(host, port, version, loader, mods);
    }
}

void MultiplayerLobbyPage::handlePendingJoin(bool success, const QString &message)
{
    m_pendingJoin = false;
    m_pendingWaitingCore = false;
    m_pendingRoomId = 0;
    m_pendingCode.clear();
    m_pendingVersion.clear();
    m_pendingLoader.clear();
    m_pendingMods.clear();

    if (!success && !message.isEmpty())
    {
        setStatus(message, true);
        AppMessageBox::information(this, tr("加入失败"), message);
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
