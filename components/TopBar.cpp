#include "TopBar.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSpacerItem>
#include <QStyle>
#include <QFontMetrics>

#include "platform.h"
#include "utils/ResourceManager.h"
#include "utils/ThemeManager.h"

TopBar::TopBar(QWidget *parent)
    : QWidget(parent)
    , m_mainLayout(nullptr)
    , m_topLayout(nullptr)
    , m_logoLabel(nullptr)
    , m_backButton(nullptr)
    , m_scrollToTopBtn(nullptr)
    , m_refreshBtn(nullptr)
    , m_translateBtn(nullptr)
    , m_titleContainer(nullptr)
    , m_searchIcon(nullptr)
    , m_titleLabel(nullptr)
    , m_titleText()
    , m_titleRotateTimer(nullptr)
    , m_searchHint()
    , m_showingHint(false)
    , m_titleOpacityEffect(nullptr)
    , m_titleAnimating(false)
    , m_accountCard(nullptr)
    , m_accountAvatar(nullptr)
    , m_accountNameLabel(nullptr)
    , m_gameCard(nullptr)
    , m_instanceNameLabel(nullptr)
    , m_instanceName()
    , m_instanceSelectBtn(nullptr)
    , m_instanceSettingsBtn(nullptr)
    , m_instanceAssistantBtn(nullptr)
    , m_launchGameBtn(nullptr)
    , m_minimizeBtn(nullptr)
    , m_maximizeBtn(nullptr)
    , m_closeBtn(nullptr)
{
    setAttribute(Qt::WA_StyledBackground);
    initUI();
}

TopBar::~TopBar()
{
}

void TopBar::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 8, 10, 8);
    m_mainLayout->setSpacing(0);

    m_topLayout = new QHBoxLayout();
    m_topLayout->setContentsMargins(0, 0, 0, 0);
    m_topLayout->setSpacing(8);

    this->setFixedHeight(56);

    initWindowControls();

    // ---- Left section: function buttons (inside func card) ----
    m_backButton = new QPushButton();
    m_backButton->setObjectName("topBarBackBtn");
    m_backButton->setIcon(ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::Back));
    m_backButton->setIconSize(QSize(18, 18));
    m_backButton->setFixedSize(28, 28);
    m_backButton->setToolTip(tr("返回上级"));
    m_backButton->hide();

    m_scrollToTopBtn = new QPushButton();
    m_scrollToTopBtn->setObjectName("topBarScrollToTopBtn");
    m_scrollToTopBtn->setIcon(ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::ScrollToTop));
    m_scrollToTopBtn->setIconSize(QSize(18, 18));
    m_scrollToTopBtn->setFixedSize(28, 28);
    m_scrollToTopBtn->setToolTip(tr("回到顶部"));

    m_refreshBtn = new QPushButton();
    m_refreshBtn->setObjectName("topBarRefreshBtn");
    m_refreshBtn->setIcon(ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::Refresh));
    m_refreshBtn->setIconSize(QSize(18, 18));
    m_refreshBtn->setFixedSize(28, 28);
    m_refreshBtn->setToolTip(tr("刷新页面"));

    m_translateBtn = new QPushButton();
    m_translateBtn->setObjectName("topBarTranslateBtn");
    m_translateBtn->setIcon(ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::Translate));
    m_translateBtn->setIconSize(QSize(18, 18));
    m_translateBtn->setFixedSize(28, 28);
    m_translateBtn->setToolTip(tr("翻译"));
    m_translateBtn->hide();

    // ---- Leftmost section: app logo ----
    m_logoLabel = new QLabel(this);
    m_logoLabel->setObjectName("topBarLogo");
    const bool isDark = (ThemeManager::instance()->currentTheme() == ThemeManager::DarkTheme);
    const QString logoPath = isDark
        ? QStringLiteral(":/Images/blockbox_icon_dark.png")
        : QStringLiteral(":/Images/logo.png");
    QPixmap logoPix(logoPath);
    m_logoLabel->setPixmap(logoPix.scaled(QSize(80, 28), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_logoLabel->setFixedSize(80, 28);
    m_logoLabel->setAlignment(Qt::AlignCenter);

    m_funcCard = createFuncCard();
    m_editionCard = createEditionCard();
    m_windowControlCard = createWindowControlCard();

    // ---- Middle section: page title card with search icon inside ----
    m_titleContainer = new QWidget();
    m_titleContainer->setObjectName("titleCardContainer");
    m_titleContainer->setCursor(Qt::PointingHandCursor);
    m_titleContainer->installEventFilter(this);

    auto *titleLayout = new QHBoxLayout(m_titleContainer);
    titleLayout->setContentsMargins(10, 6, 10, 6);
    titleLayout->setSpacing(6);

    m_searchIcon = new QLabel();
    m_searchIcon->setObjectName("titleSearchIcon");
    m_searchIcon->setFixedSize(18, 18);

    m_titleLabel = new QLabel();
    m_titleLabel->setObjectName("titleLabel");
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    // Opacity effect for fade in/out animation on title text switching
    m_titleOpacityEffect = new QGraphicsOpacityEffect(m_titleLabel);
    m_titleOpacityEffect->setOpacity(1.0);
    m_titleLabel->setGraphicsEffect(m_titleOpacityEffect);

    titleLayout->addWidget(m_searchIcon);
    titleLayout->addWidget(m_titleLabel, 1);

    // ---- Right section: account card ----
    m_accountCard = createAccountCard();

    // ---- Right section: game management card ----
    m_gameCard = createGameCard();

    // Add leftmost logo, then left section (func card + edition card)
    m_topLayout->addWidget(m_logoLabel);
    m_topLayout->addWidget(m_funcCard);
    m_topLayout->addWidget(m_editionCard);

    // Add title container (search icon + title) in the middle (stretch 1)
    m_topLayout->addWidget(m_titleContainer, 1);

    // Add right section
    m_topLayout->addWidget(m_accountCard);
    m_topLayout->addWidget(m_gameCard);

    // Window controls card (far right)
    m_topLayout->addWidget(m_windowControlCard);

    // Load search icon into the title card
    {
        QPixmap searchPix = ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::Search).pixmap(18, 18);
        m_searchIcon->setPixmap(searchPix);
    }

    // Dynamic title text rotation timer (switches between page address and search hint every 5s)
    m_titleRotateTimer = new QTimer(this);
    m_titleRotateTimer->setInterval(5000);
    connect(m_titleRotateTimer, &QTimer::timeout, this, &TopBar::rotateTitleText);
    m_titleRotateTimer->start();

    m_mainLayout->addLayout(m_topLayout);

    // Connect signals
    connect(m_backButton, &QPushButton::clicked, this, &TopBar::backClicked);
    connect(m_translateBtn, &QPushButton::clicked, this, &TopBar::translateClicked);
    connect(m_scrollToTopBtn, &QPushButton::clicked, this, &TopBar::scrollToTopClicked);
    connect(m_refreshBtn, &QPushButton::clicked, this, &TopBar::refreshClicked);
    connect(m_instanceSelectBtn, &QPushButton::clicked, this, &TopBar::instanceSelectClicked);
    connect(m_instanceSettingsBtn, &QPushButton::clicked, this, &TopBar::instanceSettingsClicked);
    connect(m_instanceAssistantBtn, &QPushButton::clicked, this, &TopBar::instanceAssistantClicked);
    connect(m_launchGameBtn, &QPushButton::clicked, this, &TopBar::launchGameClicked);

    // Account card - click on avatar or name opens account management
    connect(m_accountAvatar, &QPushButton::clicked, this, &TopBar::accountManageClicked);
    connect(m_accountNameLabel, &QPushButton::clicked, this, &TopBar::accountManageClicked);

    connect(m_minimizeBtn, &QPushButton::clicked, this, &TopBar::minimizeWindowRequested);
    connect(m_maximizeBtn, &QPushButton::clicked, this, &TopBar::maximizeWindowRequested);
    connect(m_closeBtn, &QPushButton::clicked, this, &TopBar::closeWindowRequested);

    // Refresh icons when theme text color changes
    connect(ThemeManager::instance(), &ThemeManager::textColorChanged, this, [this]() {
        QIcon themedBack = ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::Back);
        QIcon themedScroll = ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::ScrollToTop);
        QIcon themedRefresh = ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::Refresh);
        QIcon themedSelect = ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::InstanceSelect);
        QIcon themedSettings = ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::InstanceSettings);

        m_backButton->setIcon(themedBack);
        m_scrollToTopBtn->setIcon(themedScroll);
        m_refreshBtn->setIcon(themedRefresh);
        m_translateBtn->setIcon(ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::Translate));
        updateEditionCard();
        m_instanceSelectBtn->setIcon(themedSelect);
        m_instanceSettingsBtn->setIcon(themedSettings);
    m_instanceAssistantBtn->setIcon(ResourceManager::instance()->loadThemedIcon(":/Images/Icons/instance_assistant.svg"));

        // 窗口控制按钮图标也需要随主题刷新
        m_minimizeBtn->setIcon(ResourceManager::instance()->loadThemedIcon(":/Images/Icons/window_minimize.svg"));
        m_maximizeBtn->setIcon(ResourceManager::instance()->loadThemedIcon(":/Images/Icons/window_maximize.svg"));
        m_closeBtn->setIcon(ResourceManager::instance()->loadThemedIcon(":/Images/Icons/window_close.svg"));

        QPixmap searchPix = ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::Search).pixmap(18, 18);
        m_searchIcon->setPixmap(searchPix);
    });
}

bool TopBar::eventFilter(QObject *watched, QEvent *event)
{
    // 版本切换卡片：光晕层随卡片尺寸同步
    if (watched == m_editionCard && event->type() == QEvent::Resize) {
        QResizeEvent *re = static_cast<QResizeEvent *>(event);
        if (m_editionGlowJava)    m_editionGlowJava->setGeometry(0, 0, re->size().width(), re->size().height());
        if (m_editionGlowBedrock) m_editionGlowBedrock->setGeometry(0, 0, re->size().width(), re->size().height());
        return false;
    }

    // 版本切换卡片：点击卡片任意区域（含子控件）触发切换
    if (event->type() == QEvent::MouseButtonRelease && m_editionCard
        && (watched == m_editionCard
            || (watched->isWidgetType() && m_editionCard->isAncestorOf(static_cast<QWidget *>(watched))))) {
        emit editionSwitched(!m_isJavaEdition);
        return true;
    }

    if (watched == m_titleContainer && event->type() == QEvent::MouseButtonRelease)
    {
        emit searchClicked();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

QWidget* TopBar::createFuncCard()
{
    QWidget *card = new QWidget();
    card->setObjectName("funcCard");
    card->setFixedHeight(40);

    QHBoxLayout *layout = new QHBoxLayout(card);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);

    layout->addWidget(m_backButton);
    layout->addWidget(m_translateBtn);
    layout->addWidget(m_scrollToTopBtn);
    layout->addWidget(m_refreshBtn);

    return card;
}

QWidget* TopBar::createEditionCard()
{
    QWidget *card = new QWidget();
    card->setObjectName("editionCard");
    card->setFixedHeight(40);
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("javaEdition", true);
    card->installEventFilter(this);

    // 光晕层（绝对定位铺满卡片，先创建 → 绘制于内容层之下）
    m_editionGlowJava = new QLabel(card);
    m_editionGlowJava->setObjectName("editionGlowJava");
    m_editionGlowJava->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_editionGlowJava->setGeometry(0, 0, 1, 1);
    m_glowJavaOpacity = new QGraphicsOpacityEffect(m_editionGlowJava);
    m_glowJavaOpacity->setOpacity(1.0);
    m_editionGlowJava->setGraphicsEffect(m_glowJavaOpacity);

    m_editionGlowBedrock = new QLabel(card);
    m_editionGlowBedrock->setObjectName("editionGlowBedrock");
    m_editionGlowBedrock->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_editionGlowBedrock->setGeometry(0, 0, 1, 1);
    m_glowBedrockOpacity = new QGraphicsOpacityEffect(m_editionGlowBedrock);
    m_glowBedrockOpacity->setOpacity(0.0);
    m_editionGlowBedrock->setGraphicsEffect(m_glowBedrockOpacity);

    QHBoxLayout *layout = new QHBoxLayout(card);
    layout->setContentsMargins(8, 4, 10, 4);
    layout->setSpacing(7);

    // 图标区：Java（草方块）与基岩（石块）两个 QLabel 叠放
    QWidget *iconWrap = new QWidget(card);
    iconWrap->setObjectName("editionIconWrap");
    iconWrap->setFixedSize(18, 18);
    iconWrap->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_editionJavaIcon = new QLabel(iconWrap);
    m_editionJavaIcon->setObjectName("editionJavaIcon");
    m_editionJavaIcon->setFixedSize(18, 18);
    m_editionJavaIcon->setGeometry(0, 0, 18, 18);
    m_editionJavaIcon->setPixmap(ResourceManager::instance()->loadThemedIcon(":/Images/Icons/edition_java.svg").pixmap(18, 18));
    m_javaIconOpacity = new QGraphicsOpacityEffect(m_editionJavaIcon);
    m_javaIconOpacity->setOpacity(1.0);
    m_editionJavaIcon->setGraphicsEffect(m_javaIconOpacity);

    m_editionBedrockIcon = new QLabel(iconWrap);
    m_editionBedrockIcon->setObjectName("editionBedrockIcon");
    m_editionBedrockIcon->setFixedSize(18, 18);
    m_editionBedrockIcon->setGeometry(0, 0, 18, 18);
    m_editionBedrockIcon->setPixmap(ResourceManager::instance()->loadThemedIcon(":/Images/Icons/edition_bedrock.svg").pixmap(18, 18));
    m_bedrockIconOpacity = new QGraphicsOpacityEffect(m_editionBedrockIcon);
    m_bedrockIconOpacity->setOpacity(0.0);
    m_editionBedrockIcon->setGraphicsEffect(m_bedrockIconOpacity);

    layout->addWidget(iconWrap);

    // 文字区：Java/基岩 两个 QLabel 叠放，QScrollArea 裁剪实现翻牌滑动
    QScrollArea *textWrap = new QScrollArea(card);
    textWrap->setObjectName("editionTextWrap");
    textWrap->setFixedSize(52, 20);
    textWrap->setFrameShape(QFrame::NoFrame);
    textWrap->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    textWrap->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    textWrap->viewport()->setAutoFillBackground(false);
    textWrap->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    textWrap->viewport()->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    QWidget *vp = textWrap->viewport();
    m_editionJavaText = new QLabel(tr("Java版"), vp);
    m_editionJavaText->setObjectName("editionJavaText");
    m_editionJavaText->setFixedSize(52, 20);
    m_editionJavaText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_editionJavaText->setGeometry(0, 0, 52, 20);
    m_javaTextOpacity = new QGraphicsOpacityEffect(m_editionJavaText);
    m_javaTextOpacity->setOpacity(1.0);
    m_editionJavaText->setGraphicsEffect(m_javaTextOpacity);

    m_editionBedrockText = new QLabel(tr("基岩版"), vp);
    m_editionBedrockText->setObjectName("editionBedrockText");
    m_editionBedrockText->setFixedSize(52, 20);
    m_editionBedrockText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_editionBedrockText->setGeometry(0, 20, 52, 20);
    m_bedrockTextOpacity = new QGraphicsOpacityEffect(m_editionBedrockText);
    m_bedrockTextOpacity->setOpacity(0.0);
    m_editionBedrockText->setGraphicsEffect(m_bedrockTextOpacity);

    layout->addWidget(textWrap);

    // 状态圆点
    m_editionDot = new QLabel(card);
    m_editionDot->setObjectName("editionDot");
    m_editionDot->setFixedSize(8, 8);
    m_editionDot->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_editionDot->setProperty("javaEdition", true);
    m_dotOpacity = new QGraphicsOpacityEffect(m_editionDot);
    m_dotOpacity->setOpacity(1.0);
    m_editionDot->setGraphicsEffect(m_dotOpacity);
    layout->addWidget(m_editionDot);

    layout->addStretch();

    return card;
}

void TopBar::updateEditionCard()
{
    if (!m_editionCard)
        return;

    const bool java = m_isJavaEdition;

    // 卡片与圆点的版本属性（QSS 属性选择器刷新边框/背景色）
    m_editionCard->setProperty("javaEdition", java);
    m_editionCard->style()->unpolish(m_editionCard);
    m_editionCard->style()->polish(m_editionCard);
    if (m_editionDot) {
        m_editionDot->setProperty("javaEdition", java);
        m_editionDot->style()->unpolish(m_editionDot);
        m_editionDot->style()->polish(m_editionDot);
    }

    // 无动画的终态同步（外部直接调用时使用，动画由 animateEditionSwitch 接管）
    if (m_glowJavaOpacity)     m_glowJavaOpacity->setOpacity(java ? 1.0 : 0.0);
    if (m_glowBedrockOpacity)  m_glowBedrockOpacity->setOpacity(java ? 0.0 : 1.0);
    if (m_javaIconOpacity)     m_javaIconOpacity->setOpacity(java ? 1.0 : 0.0);
    if (m_bedrockIconOpacity)  m_bedrockIconOpacity->setOpacity(java ? 0.0 : 1.0);
    if (m_javaTextOpacity)     m_javaTextOpacity->setOpacity(java ? 1.0 : 0.0);
    if (m_bedrockTextOpacity)  m_bedrockTextOpacity->setOpacity(java ? 0.0 : 1.0);
    if (m_editionJavaIcon)     m_editionJavaIcon->move(0, 0);
    if (m_editionBedrockIcon)  m_editionBedrockIcon->move(0, 0);
    if (m_editionJavaText)     m_editionJavaText->move(0, 0);
    if (m_editionBedrockText)  m_editionBedrockText->move(0, java ? 20 : 0);
    if (m_dotOpacity)          m_dotOpacity->setOpacity(1.0);

    // 基岩版模式隐藏顶栏账户卡片
    if (m_accountCard) m_accountCard->setVisible(java);
}

void TopBar::setJavaEdition(bool java)
{
    if (m_isJavaEdition == java)
        return;
    m_isJavaEdition = java;
    animateEditionSwitch();
    updateGameCardForEdition();
    if (m_accountCard) m_accountCard->setVisible(java);
}

void TopBar::toggleEdition()
{
    emit editionSwitched(!m_isJavaEdition);
}

void TopBar::animateEditionSwitch()
{
    if (!m_editionCard)
        return;

    stopEditionAnimation();

    const bool toJava = m_isJavaEdition;
    const double fromOp = toJava ? 0.0 : 1.0;  // 目标元素当前透明度
    const double toOp   = toJava ? 1.0 : 0.0;  // 目标元素切换后透明度
    const QPoint javaIconShow(0, 0),  javaIconHide(-2, 2);
    const QPoint bedrockIconShow(0, 0), bedrockIconHide(2, -2);
    const QPoint javaTextShow(0, 0),  javaTextHide(0, -10);
    const QPoint bedrockTextShow(0, 0), bedrockTextHide(0, 10);

    auto *group = new QParallelAnimationGroup(this);
    group->setDirection(QAbstractAnimation::Forward);

    // 光晕交叉淡入淡出
    QPropertyAnimation *glowJava = new QPropertyAnimation(m_glowJavaOpacity, "opacity");
    glowJava->setDuration(240);
    glowJava->setStartValue(fromOp);
    glowJava->setEndValue(toOp);
    glowJava->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(glowJava);

    QPropertyAnimation *glowBedrock = new QPropertyAnimation(m_glowBedrockOpacity, "opacity");
    glowBedrock->setDuration(240);
    glowBedrock->setStartValue(toOp);
    glowBedrock->setEndValue(fromOp);
    glowBedrock->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(glowBedrock);

    // Java 图标：淡出 + 向左下滑动
    QPropertyAnimation *iconJava = new QPropertyAnimation(m_editionJavaIcon, "pos");
    iconJava->setDuration(240);
    iconJava->setStartValue(toJava ? javaIconHide : javaIconShow);
    iconJava->setEndValue(toJava ? javaIconShow : javaIconHide);
    iconJava->setEasingCurve(QEasingCurve::InCubic);
    group->addAnimation(iconJava);

    QPropertyAnimation *iconJavaOp = new QPropertyAnimation(m_javaIconOpacity, "opacity");
    iconJavaOp->setDuration(200);
    iconJavaOp->setStartValue(toOp);
    iconJavaOp->setEndValue(fromOp);
    iconJavaOp->setEasingCurve(QEasingCurve::InQuad);
    group->addAnimation(iconJavaOp);

    // 基岩图标：从右上弹入（OutBack 带回弹）
    QPropertyAnimation *iconBedrock = new QPropertyAnimation(m_editionBedrockIcon, "pos");
    iconBedrock->setDuration(260);
    iconBedrock->setStartValue(toJava ? bedrockIconHide : bedrockIconShow);
    iconBedrock->setEndValue(toJava ? bedrockIconShow : bedrockIconHide);
    iconBedrock->setEasingCurve(QEasingCurve::OutBack);
    group->addAnimation(iconBedrock);

    QPropertyAnimation *iconBedrockOp = new QPropertyAnimation(m_bedrockIconOpacity, "opacity");
    iconBedrockOp->setDuration(240);
    iconBedrockOp->setStartValue(fromOp);
    iconBedrockOp->setEndValue(toOp);
    iconBedrockOp->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(iconBedrockOp);

    // Java 文字：上滑淡出
    QPropertyAnimation *textJava = new QPropertyAnimation(m_editionJavaText, "pos");
    textJava->setDuration(240);
    textJava->setStartValue(toJava ? javaTextHide : javaTextShow);
    textJava->setEndValue(toJava ? javaTextShow : javaTextHide);
    textJava->setEasingCurve(QEasingCurve::InCubic);
    group->addAnimation(textJava);

    QPropertyAnimation *textJavaOp = new QPropertyAnimation(m_javaTextOpacity, "opacity");
    textJavaOp->setDuration(200);
    textJavaOp->setStartValue(toOp);
    textJavaOp->setEndValue(fromOp);
    textJavaOp->setEasingCurve(QEasingCurve::InQuad);
    group->addAnimation(textJavaOp);

    // 基岩文字：下滑淡入
    QPropertyAnimation *textBedrock = new QPropertyAnimation(m_editionBedrockText, "pos");
    textBedrock->setDuration(240);
    textBedrock->setStartValue(toJava ? bedrockTextShow : bedrockTextHide);
    textBedrock->setEndValue(toJava ? bedrockTextHide : bedrockTextShow);
    textBedrock->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(textBedrock);

    QPropertyAnimation *textBedrockOp = new QPropertyAnimation(m_bedrockTextOpacity, "opacity");
    textBedrockOp->setDuration(240);
    textBedrockOp->setStartValue(fromOp);
    textBedrockOp->setEndValue(toOp);
    textBedrockOp->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(textBedrockOp);

    // 状态圆点：弹跳强调（颜色由 QSS 属性选择器即时切换）
    // 注意：QLabel 本身没有 opacity 属性，动画应作用于其 QGraphicsOpacityEffect (m_dotOpacity)
    QPropertyAnimation *dotOp = new QPropertyAnimation(m_dotOpacity, "opacity");
    dotOp->setDuration(300);
    dotOp->setStartValue(0.35);
    dotOp->setEndValue(1.0);
    dotOp->setEasingCurve(QEasingCurve::OutBack);
    group->addAnimation(dotOp);

    connect(group, &QParallelAnimationGroup::finished, this, [this]() {
        m_editionAnimating = false;
        m_editionAnimGroup = nullptr;
        updateEditionCard();
    });

    m_editionAnimGroup = group;
    m_editionAnimating = true;
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void TopBar::stopEditionAnimation()
{
    if (m_editionAnimGroup) {
        disconnect(m_editionAnimGroup.data(), &QParallelAnimationGroup::finished, this, nullptr);
        m_editionAnimGroup->stop();
        m_editionAnimGroup->deleteLater();
        m_editionAnimGroup = nullptr;
    }
    m_editionAnimating = false;
}

QWidget* TopBar::createAccountCard()
{
    QWidget *card = new QWidget();
    card->setObjectName("accountCard");
    card->setFixedHeight(40);

    QHBoxLayout *layout = new QHBoxLayout(card);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    m_accountAvatar = new QPushButton();
    m_accountAvatar->setObjectName("accountAvatar");
    m_accountAvatar->setFixedSize(28, 28);
    m_accountAvatar->setIconSize(QSize(28, 28));
    m_accountAvatar->setCheckable(true);
    m_accountAvatar->setToolTip(tr("账户管理"));

    m_accountNameLabel = new QPushButton(tr("未登录"));
    m_accountNameLabel->setObjectName("accountNameLabel");
    m_accountNameLabel->setFlat(true);
    m_accountNameLabel->setCursor(Qt::PointingHandCursor);

    layout->addWidget(m_accountAvatar);
    layout->addWidget(m_accountNameLabel);

    return card;
}

QWidget* TopBar::createGameCard()
{
    QWidget *card = new QWidget();
    card->setObjectName("gameCard");
    card->setFixedHeight(40);

    QHBoxLayout *layout = new QHBoxLayout(card);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(4);

    m_instanceName = tr("未选择实例");
    m_instanceNameLabel = new QLabel(m_instanceName);
    m_instanceNameLabel->setObjectName("instanceNameLabel");
    m_instanceNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_instanceNameLabel->setToolTip(m_instanceName);

    m_instanceSelectBtn = new QPushButton();
    m_instanceSelectBtn->setObjectName("instanceSelectBtn");
    m_instanceSelectBtn->setIcon(ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::InstanceSelect));
    m_instanceSelectBtn->setIconSize(QSize(16, 16));
    m_instanceSelectBtn->setFixedSize(24, 24);
    m_instanceSelectBtn->setCheckable(true);
    m_instanceSelectBtn->setToolTip(tr("选择实例"));

    m_instanceSettingsBtn = new QPushButton();
    m_instanceSettingsBtn->setObjectName("instanceSettingsBtn");
    m_instanceSettingsBtn->setIcon(ResourceManager::instance()->loadThemedIcon(ResourceManager::IconType::InstanceSettings));
    m_instanceSettingsBtn->setIconSize(QSize(16, 16));
    m_instanceSettingsBtn->setFixedSize(24, 24);
    m_instanceSettingsBtn->setCheckable(true);
    m_instanceSettingsBtn->setToolTip(tr("实例设置"));

    m_instanceAssistantBtn = new QPushButton();
    m_instanceAssistantBtn->setObjectName("instanceAssistantBtn");
    m_instanceAssistantBtn->setIcon(ResourceManager::instance()->loadThemedIcon(":/Images/Icons/instance_assistant.svg"));
    m_instanceAssistantBtn->setIconSize(QSize(16, 16));
    m_instanceAssistantBtn->setFixedSize(24, 24);
    m_instanceAssistantBtn->setCheckable(true);
    m_instanceAssistantBtn->setToolTip(tr("实例助手"));

    m_launchGameBtn = new QPushButton(tr("启动游戏"));
    m_launchGameBtn->setObjectName("launchGameBtn");
    m_launchGameBtn->setFixedHeight(32);

    layout->addWidget(m_instanceNameLabel);
    layout->addWidget(m_instanceSelectBtn);
    layout->addWidget(m_instanceSettingsBtn);
    layout->addWidget(m_instanceAssistantBtn);
    layout->addWidget(m_launchGameBtn);

    return card;
}

QWidget* TopBar::createWindowControlCard()
{
    QWidget *card = new QWidget();
    card->setObjectName("windowControlCard");
    card->setFixedHeight(40);

    QHBoxLayout *layout = new QHBoxLayout(card);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    layout->addWidget(m_minimizeBtn);
    layout->addWidget(m_maximizeBtn);
    layout->addWidget(m_closeBtn);

    return card;
}

void TopBar::initWindowControls()
{
    m_minimizeBtn = new QPushButton();
    m_maximizeBtn = new QPushButton();
    m_closeBtn = new QPushButton();

    applyPlatformStyle();

    m_minimizeBtn->setToolTip(tr("最小化"));
    m_maximizeBtn->setToolTip(tr("最大化"));
    m_closeBtn->setToolTip(tr("关闭"));

    m_minimizeBtn->setFixedSize(28, 28);
    m_maximizeBtn->setFixedSize(28, 28);
    m_closeBtn->setFixedSize(28, 28);

    m_minimizeBtn->setIconSize(QSize(16, 16));
    m_maximizeBtn->setIconSize(QSize(16, 16));
    m_closeBtn->setIconSize(QSize(16, 16));
}

void TopBar::applyPlatformStyle()
{
    // 所有平台统一使用 SVG 图标，通过 loadThemedIcon 自动适配主题色
    QIcon minimizeIcon = ResourceManager::instance()->loadThemedIcon(":/Images/Icons/window_minimize.svg");
    QIcon maximizeIcon = ResourceManager::instance()->loadThemedIcon(":/Images/Icons/window_maximize.svg");
    QIcon closeIcon    = ResourceManager::instance()->loadThemedIcon(":/Images/Icons/window_close.svg");

#ifdef PLATFORM_WINDOWS
    {
        m_minimizeBtn->setObjectName("minimizeBtn");
        m_maximizeBtn->setObjectName("maximizeBtn");
        m_closeBtn->setObjectName("closeBtn");
    }
#elif defined(PLATFORM_MAC)
    {
        m_minimizeBtn->setObjectName("macMinimizeBtn");
        m_maximizeBtn->setObjectName("macMaximizeBtn");
        m_closeBtn->setObjectName("macCloseBtn");
    }
#elif defined(PLATFORM_LINUX)
    {
        m_minimizeBtn->setObjectName("linuxMinimizeBtn");
        m_maximizeBtn->setObjectName("linuxMaximizeBtn");
        m_closeBtn->setObjectName("linuxCloseBtn");
    }
#elif defined(PLATFORM_HARMONY)
    {
        m_minimizeBtn->setObjectName("harmonyMinimizeBtn");
        m_maximizeBtn->setObjectName("harmonyMaximizeBtn");
        m_closeBtn->setObjectName("harmonyCloseBtn");
    }
#else
    {
        m_minimizeBtn->setObjectName("defaultMinimizeBtn");
        m_maximizeBtn->setObjectName("defaultMaximizeBtn");
        m_closeBtn->setObjectName("defaultCloseBtn");
    }
#endif

    m_minimizeBtn->setIcon(minimizeIcon);
    m_maximizeBtn->setIcon(maximizeIcon);
    m_closeBtn->setIcon(closeIcon);
}

void TopBar::setStatusText(const QString &text)
{
    Q_UNUSED(text);
}

void TopBar::setInstanceName(const QString &name)
{
    m_instanceName = name;
    if (m_instanceNameLabel) {
        m_instanceNameLabel->setToolTip(name);
        updateInstanceNameElision();
    }
}

void TopBar::setBedrockInstanceName(const QString &name)
{
    m_bedrockInstanceName = name;
    if (!m_isJavaEdition && m_instanceNameLabel)
        updateGameCardForEdition();
}

void TopBar::updateGameCardForEdition()
{
    if (!m_gameCard)
        return;

    const bool java = m_isJavaEdition;

    if (m_instanceSelectBtn) {
        m_instanceSelectBtn->setVisible(true);
        m_instanceSelectBtn->setToolTip(java ? tr("选择实例") : tr("选择基岩版实例"));
    }
    if (m_instanceSettingsBtn) {
        m_instanceSettingsBtn->setVisible(true);
        m_instanceSettingsBtn->setToolTip(java ? tr("实例设置") : tr("基岩版实例设置"));
    }

    // 基岩版：实例名显示当前激活的基岩版实例名，否则显示“基岩版”
    if (!java) {
        if (m_instanceNameLabel) {
            const QString text = m_bedrockInstanceName.isEmpty() ? tr("基岩版") : m_bedrockInstanceName;
            m_instanceNameLabel->setText(text);
            m_instanceNameLabel->setToolTip(text);
        }
    } else {
        if (m_instanceNameLabel) {
            m_instanceNameLabel->setToolTip(m_instanceName);
            updateInstanceNameElision();
        }
    }
}

void TopBar::setMainTitle()
{
    m_titleText = tr("方块盒子");
    m_searchHint = tr("搜索所有内容");
    m_showingHint = false;
    stopTitleAnimation();
    if (m_titleRotateTimer) m_titleRotateTimer->start();
    updateTitleElision();
    m_backButton->hide();
    m_translateBtn->hide();
}

void TopBar::setSettingsTitle()
{
    m_titleText = tr("方块盒子>设置");
    m_searchHint = tr("搜索设置项以及其他内容");
    m_showingHint = false;
    stopTitleAnimation();
    if (m_titleRotateTimer) m_titleRotateTimer->start();
    updateTitleElision();
    m_backButton->show();
}

void TopBar::setResourcesTitle()
{
    m_titleText = tr("方块盒子>资源");
    m_searchHint = tr("搜索资源以及其他内容");
    m_showingHint = false;
    stopTitleAnimation();
    if (m_titleRotateTimer) m_titleRotateTimer->start();
    updateTitleElision();
    m_backButton->show();
}

void TopBar::setInstanceSelectTitle()
{
    m_titleText = tr("方块盒子>选择实例");
    m_searchHint = tr("搜索实例以及其他内容");
    m_showingHint = false;
    stopTitleAnimation();
    if (m_titleRotateTimer) m_titleRotateTimer->start();
    updateTitleElision();
    m_backButton->show();
}

void TopBar::setAccountManageTitle()
{
    m_titleText = tr("方块盒子>账户管理");
    m_searchHint = tr("搜索账户以及其他内容");
    m_showingHint = false;
    stopTitleAnimation();
    if (m_titleRotateTimer) m_titleRotateTimer->start();
    updateTitleElision();
    m_backButton->show();
}

void TopBar::setAddAccountTitle()
{
    m_titleText = tr("方块盒子>账户管理>添加账户");
    m_searchHint = tr("搜索账户以及其他内容");
    m_showingHint = false;
    stopTitleAnimation();
    if (m_titleRotateTimer) m_titleRotateTimer->start();
    updateTitleElision();
    m_backButton->show();
}

void TopBar::setLaunchDetailsTitle()
{
    m_titleText = tr("方块盒子>启动详情");
    m_searchHint = tr("搜索启动详情以及其他内容");
    m_showingHint = false;
    stopTitleAnimation();
    if (m_titleRotateTimer) m_titleRotateTimer->start();
    updateTitleElision();
    m_backButton->show();
}

void TopBar::setTaskListTitle()
{
    m_titleText = tr("方块盒子>任务列表");
    m_searchHint = tr("搜索任务以及其他内容");
    m_showingHint = false;
    stopTitleAnimation();
    if (m_titleRotateTimer) m_titleRotateTimer->start();
    updateTitleElision();
    m_backButton->show();
}

void TopBar::setTitle(const QString &title)
{
    m_titleText = tr("方块盒子>%1").arg(title);
    setSearchHintForTitle(title);
    m_showingHint = false;
    stopTitleAnimation();
    if (m_titleRotateTimer) m_titleRotateTimer->start();
    updateTitleElision();
    m_backButton->show();
}

void TopBar::updateTitleElision()
{
    QString displayText = m_showingHint && !m_searchHint.isEmpty() ? m_searchHint : m_titleText;

    QFontMetrics fm(m_titleLabel->font());
    int availableWidth = m_titleLabel->width() - 32; // 16px padding on each side
    if (availableWidth < 10)
        availableWidth = 10;
    int textWidth = fm.horizontalAdvance(displayText);
    if (textWidth <= availableWidth) {
        m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_titleLabel->setText(displayText);
    } else {
        m_titleLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_titleLabel->setText(fm.elidedText(displayText, Qt::ElideLeft, availableWidth));
    }
}

void TopBar::rotateTitleText()
{
    if (m_searchHint.isEmpty())
        return;
    animateTitleSwitch();
}

void TopBar::animateTitleSwitch()
{
    if (m_titleAnimating || !m_titleOpacityEffect)
        return;
    m_titleAnimating = true;

    auto *group = new QSequentialAnimationGroup(this);

    // Phase 1: fade out current text (200ms)
    auto *fadeOut = new QPropertyAnimation(m_titleOpacityEffect, "opacity");
    fadeOut->setDuration(200);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InQuad);
    group->addAnimation(fadeOut);

    // Switch the displayed text when opacity is at the lowest point
    connect(fadeOut, &QPropertyAnimation::finished, this, [this]() {
        m_showingHint = !m_showingHint;
        updateTitleElision();
    });

    // Phase 2: fade in the new text (300ms)
    auto *fadeIn = new QPropertyAnimation(m_titleOpacityEffect, "opacity");
    fadeIn->setDuration(300);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutQuad);
    group->addAnimation(fadeIn);

    connect(group, &QSequentialAnimationGroup::finished, this, [this]() {
        m_titleAnimating = false;
        m_titleAnimGroup = nullptr;
    });

    m_titleAnimGroup = group;
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void TopBar::stopTitleAnimation()
{
    if (m_titleAnimGroup) {
        disconnect(m_titleAnimGroup.data(), &QSequentialAnimationGroup::finished, this, nullptr);
        m_titleAnimGroup->stop();
        m_titleAnimGroup->deleteLater();
        m_titleAnimGroup = nullptr;
    }
    m_titleAnimating = false;
    if (m_titleOpacityEffect)
        m_titleOpacityEffect->setOpacity(1.0);
}

void TopBar::setSearchHintForTitle(const QString &title)
{
    // Infer search hint from the page title prefix
    if (title.contains(tr("设置")))
        m_searchHint = tr("搜索设置项以及其他内容");
    else if (title.contains(tr("资源")))
        m_searchHint = tr("搜索资源以及其他内容");
    else if (title.contains(tr("实例")))
        m_searchHint = tr("搜索实例以及其他内容");
    else if (title.contains(tr("账户")))
        m_searchHint = tr("搜索账户以及其他内容");
    else if (title.contains(tr("任务")))
        m_searchHint = tr("搜索任务以及其他内容");
    else if (title.contains(tr("AI"), Qt::CaseInsensitive))
        m_searchHint = tr("搜索AI助手以及其他内容");
    else if (title.contains(tr("启动")))
        m_searchHint = tr("搜索启动详情以及其他内容");
    else
        m_searchHint = tr("搜索相关内容");
}

void TopBar::updateInstanceNameElision()
{
    QFontMetrics fm(m_instanceNameLabel->font());
    int availableWidth = m_instanceNameLabel->width();
    if (availableWidth < 10)
        availableWidth = 10;
    m_instanceNameLabel->setText(fm.elidedText(m_instanceName, Qt::ElideRight, availableWidth));
}

void TopBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTitleElision();
    updateInstanceNameElision();
}

void TopBar::setAccountName(const QString &name)
{
    if (m_accountNameLabel) m_accountNameLabel->setText(name);
}

QString TopBar::accountName() const
{
    return m_accountNameLabel ? m_accountNameLabel->text() : QString();
}

void TopBar::setAccountAvatar(const QPixmap &pixmap)
{
    if (!m_accountAvatar || pixmap.isNull())
        return;

    const int size = 28;

    // 皮肤头像本身为正方形，铺满后按圆角路径裁剪出带圆角的头像
    QPixmap rounded(size, size);
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath clipPath;
    clipPath.addRoundedRect(0, 0, size, size, 6, 6);
    painter.setClipPath(clipPath);
    painter.drawPixmap(rounded.rect(), pixmap.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    painter.end();

    m_accountAvatar->setIcon(QIcon(rounded));
    m_accountAvatar->setIconSize(QSize(size, size));
}

void TopBar::setAccountCardVisible(bool visible)
{
    if (!m_accountCard) return;
    // 基岩版模式下始终隐藏账户卡片
    m_accountCard->setVisible(visible && m_isJavaEdition);
}

void TopBar::setWindowControlCardVisible(bool visible)
{
    if (m_windowControlCard) m_windowControlCard->setVisible(visible);
}

void TopBar::setInstanceSettingsSelected(bool selected)
{
    if (m_instanceSettingsBtn) m_instanceSettingsBtn->setChecked(selected);
}

void TopBar::setInstanceSelectSelected(bool selected)
{
    if (m_instanceSelectBtn) m_instanceSelectBtn->setChecked(selected);
}

void TopBar::setAccountSelected(bool selected)
{
    if (m_accountAvatar) m_accountAvatar->setChecked(selected);
    if (m_accountCard) {
        m_accountCard->setProperty("selected", selected);
        m_accountCard->style()->unpolish(m_accountCard);
        m_accountCard->style()->polish(m_accountCard);
    }
}

void TopBar::setTranslateButtonVisible(bool visible)
{
    if (m_translateBtn) m_translateBtn->setVisible(visible);
}
