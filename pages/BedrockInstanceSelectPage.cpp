/**
 * @file   BedrockInstanceSelectPage.cpp
 * @brief  基岩版实例选择页实现 — 布局/样式与 Java 版实例选择页一致
 * @author BlockBox Team
 */

#include "BedrockInstanceSelectPage.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include "../components/AppFileDialog.h"
#include "../components/AppInputDialog.h"
#include "../components/AppMessageBox.h"
#include "../components/MasonryContentCard.h"
#include "../components/NotificationManager.h"
#include "utils/BedrockLauncher.h"
#include "utils/IconHelper.h"
#include "utils/ThemeManager.h"
#include "platform.h"

namespace {
const QColor kBedrockAccent(31, 167, 143); // 基岩版青色
}

BedrockInstanceSelectPage::BedrockInstanceSelectPage(QWidget *parent)
    : QWidget(parent)
    , m_leftWidget(nullptr)
    , m_folderHighlight(nullptr)
    , m_viewSwitch(nullptr)
{
    initUI();
    refresh();

    connect(&m_sizeWatcher, &QFutureWatcher<QMap<QString, qint64>>::finished,
            this, &BedrockInstanceSelectPage::onAsyncSizesCalculated);
    connect(BedrockInstanceManager::instance(), &BedrockInstanceManager::instancesChanged,
            this, &BedrockInstanceSelectPage::onManagerChanged);
}

void BedrockInstanceSelectPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_shown) {
        m_shown = true;
        BedrockInstanceManager::instance()->ensureInitialized();
    }
    refresh();
    // 每次显示时重新贴靠气泡几何（页面切换动画期间尺寸可能变化）
    QTimer::singleShot(0, this, [this]() { updateLeftBubbleGeometry(); });
}

void BedrockInstanceSelectPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLeftBubbleGeometry();
}

void BedrockInstanceSelectPage::updateLeftBubbleGeometry()
{
    if (!m_leftWidget)
        return;
    const int x = kBubbleMargin;
    const int y = kBubbleMargin;
    const int h = qMax(120, height() - 2 * kBubbleMargin);
    m_leftWidget->setGeometry(x, y, kBubbleWidth, h);
    m_leftWidget->raise();
}

void BedrockInstanceSelectPage::initUI()
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    initLeftSidebar();
    initRightMainBar();

    // 左侧悬浮气泡（与 InstanceSelectPage 一致：浮动定位 + 投影）
    m_leftWidget = new QWidget(this);
    m_leftWidget->setObjectName("instanceLeftSidebar");
    m_leftWidget->setLayout(m_leftLayout);
    m_leftWidget->setAttribute(Qt::WA_StyledBackground);
    m_leftWidget->setFixedWidth(kBubbleWidth);

    auto *shadow = new QGraphicsDropShadowEffect(m_leftWidget);
    shadow->setBlurRadius(20);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 50));
    m_leftWidget->setGraphicsEffect(shadow);

    m_mainLayout->setContentsMargins(kBubbleWidth + kBubbleMargin, 0, 0, 0);

    QWidget *rightWidget = new QWidget();
    rightWidget->setLayout(m_rightLayout);
    rightWidget->setMinimumWidth(600);

    m_mainLayout->addWidget(rightWidget, 1);
    m_mainLayout->setStretchFactor(rightWidget, 1);

    // 滑动高亮背景（与其他子导航栏一致的选中指示器）
    m_folderHighlight = new QWidget(m_leftWidget);
    m_folderHighlight->setObjectName("instanceFolderHighlight");
    m_folderHighlight->setVisible(false);
    // 高亮条只做视觉指示，不能拦截鼠标事件
    m_folderHighlight->setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void BedrockInstanceSelectPage::initLeftSidebar()
{
    m_leftLayout = new QVBoxLayout();
    m_leftLayout->setContentsMargins(8, 12, 8, 12);
    m_leftLayout->setSpacing(6);

    QLabel *sectionTitle = new QLabel(tr("基岩版实例"));
    sectionTitle->setObjectName("instanceSidebarSectionTitle");
    m_leftLayout->addWidget(sectionTitle);

    // 全部实例（当前唯一的导航项，保持与实例文件夹导航按钮一致的样式）
    QPushButton *allBtn = new QPushButton(tr("全部实例"));
    allBtn->setObjectName("instanceFolderNavBtn");
    allBtn->setFixedHeight(34);
    allBtn->setCheckable(true);
    allBtn->setChecked(true);
    allBtn->setCursor(Qt::PointingHandCursor);
    allBtn->setIconSize(QSize(16, 16));
    allBtn->setIcon(loadColoredIcon(":/Images/Icons/edition_bedrock.svg", QColor("#1FA78F")));
    m_leftLayout->addWidget(allBtn);

    m_leftLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));

    // 底部操作区（与"绑定文件夹"按钮位置一致）
    QFrame *bottomSeparator = new QFrame();
    bottomSeparator->setObjectName("instanceSidebarSeparator");
    bottomSeparator->setFixedHeight(1);
    m_leftLayout->addWidget(bottomSeparator);

    QPushButton *dataDirBtn = new QPushButton(tr("打开数据目录"));
    dataDirBtn->setObjectName("instanceBindFolderBtn");
    dataDirBtn->setFixedHeight(34);
    dataDirBtn->setIconSize(QSize(16, 16));
    dataDirBtn->setIcon(loadColoredIcon(":/Images/Icons/nav_folder.svg", QColor("#666666")));
    dataDirBtn->setCursor(Qt::PointingHandCursor);
    m_leftLayout->addWidget(dataDirBtn);

    connect(dataDirBtn, &QPushButton::clicked, this, &BedrockInstanceSelectPage::onOpenDataDirClicked);

    // 初始化滑动高亮位置（延迟到下一次事件循环以确保 geometry 就绪）
    QTimer::singleShot(0, this, [this, allBtn]() { slideHighlightTo(allBtn); });
}

void BedrockInstanceSelectPage::slideHighlightTo(QPushButton *target)
{
    if (!target || !m_folderHighlight)
        return;

    const QRect endRect = target->geometry();
    if (!endRect.isValid() || endRect.width() <= 0) {
        QPushButton *btnPtr = target;
        QTimer::singleShot(0, this, [this, btnPtr]() { slideHighlightTo(btnPtr); });
        return;
    }

    m_folderHighlight->setVisible(true);
    m_folderHighlight->setGeometry(endRect);
}

void BedrockInstanceSelectPage::initRightMainBar()
{
    m_rightLayout = new QVBoxLayout();
    m_rightLayout->setContentsMargins(10, 10, 10, 10);
    m_rightLayout->setSpacing(10);

    // ── 顶部工具条：视图切换 + 操作按钮（与 Java 版一致） ──
    m_topLayout = new QHBoxLayout();
    m_topLayout->setContentsMargins(0, 0, 0, 10);
    m_topLayout->setSpacing(10);

    m_viewSwitch = new ContentViewSwitch(this);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted("bedrock_instance_select",
                                                               ContentViewSwitch::List));
    m_viewMode = static_cast<int>(m_viewSwitch->viewMode());
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &BedrockInstanceSelectPage::onViewModeChanged);
    m_topLayout->addWidget(m_viewSwitch);

    m_topLayout->addStretch();

    QPushButton *newBtn = new QPushButton(tr("新建实例"));
    newBtn->setObjectName("instanceActionBtn");
    QPushButton *importBtn = new QPushButton(tr("导入游戏数据"));
    importBtn->setObjectName("instanceActionBtn");

    m_topLayout->addWidget(newBtn);
    m_topLayout->addWidget(importBtn);
    m_rightLayout->addLayout(m_topLayout);

    // 分隔线
    QFrame *separator = new QFrame();
    separator->setObjectName("instanceListSeparator");
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    m_rightLayout->addWidget(separator);

    // ── 实例列表区（列表 / 瀑布流 / 空状态） ──
    m_instanceScrollArea = new QScrollArea();
    m_instanceScrollArea->setObjectName("instanceScrollArea");
    m_instanceScrollArea->setWidgetResizable(true);

    m_instanceContainer = new QWidget();
    m_instanceContainer->setObjectName("instanceContainer");

    m_instanceListWidget = new QListWidget();
    m_instanceListWidget->setObjectName("instanceListWidget");
    m_instanceListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_instanceListWidget->setAlternatingRowColors(false);
    m_instanceListWidget->setUniformItemSizes(true);
    m_instanceListWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_instanceListWidget->setMovement(QListView::Static);
    m_instanceListWidget->setViewMode(QListView::ListMode);
    m_instanceListWidget->setIconSize(QSize(48, 48));
    m_instanceListWidget->setSpacing(8);

    // 瀑布流容器（页 1）
    m_masonryContainer = new QWidget();
    m_masonryContainer->setObjectName("instanceMasonryContainer");
    m_masonryLayout = new MasonryLayout(m_masonryContainer, 0, 12, 12);
    m_masonryLayout->setContentsMargins(0, 0, 0, 0);

    // 空状态（页 2）
    m_emptyState = new QWidget();
    QVBoxLayout *emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(32, 48, 32, 48);
    QLabel *emptyLabel = new QLabel(tr("还没有基岩版实例\n点击「新建实例」开始，或「导入游戏数据」接入已有的游戏存档。"), m_emptyState);
    emptyLabel->setObjectName("sectionSubtitle");
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyLabel);
    emptyLayout->addStretch();

    // 双视图 + 空状态
    m_viewStack = new QStackedWidget();
    m_viewStack->addWidget(m_instanceListWidget);
    m_viewStack->addWidget(m_masonryContainer);
    m_viewStack->addWidget(m_emptyState);
    m_viewStack->setCurrentIndex(m_viewMode == ContentViewSwitch::Masonry ? 1 : 0);

    QVBoxLayout *scrollLayout = new QVBoxLayout(m_instanceContainer);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->addWidget(m_viewStack);

    m_instanceScrollArea->setWidget(m_instanceContainer);
    m_rightLayout->addWidget(m_instanceScrollArea);

    // 信号连接
    connect(m_instanceListWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            launchInstance(id);
    });
    m_instanceListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_instanceListWidget, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_instanceListWidget->itemAt(pos);
        if (!item)
            return;
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty())
            return;

        QMenu menu(this);
        menu.setStyleSheet(m_instanceListWidget->styleSheet());
        QAction *launchAction = menu.addAction(tr("启动游戏"));
        menu.addSeparator();
        QAction *openAction = menu.addAction(tr("打开实例文件夹"));
        QAction *renameAction = menu.addAction(tr("重命名"));
        menu.addSeparator();
        QAction *deleteAction = menu.addAction(tr("删除实例"));
        deleteAction->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#F44336"), 16));

        QAction *chosen = menu.exec(m_instanceListWidget->mapToGlobal(pos));
        if (chosen == launchAction)
            launchInstance(id);
        else if (chosen == openAction)
            openInstanceFolder(id);
        else if (chosen == renameAction)
            renameInstance(id);
        else if (chosen == deleteAction)
            deleteInstance(id);
    });

    connect(newBtn, &QPushButton::clicked, this, &BedrockInstanceSelectPage::onCreateInstanceClicked);
    connect(importBtn, &QPushButton::clicked, this, &BedrockInstanceSelectPage::onImportInstanceClicked);
}

void BedrockInstanceSelectPage::refresh()
{
    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    mgr->ensureInitialized();

    // 安装状态仅首次查询，避免反复调用 PowerShell
    if (!m_versionResolved) {
        m_versionResolved = true;
        BedrockLauncher *bedrock = BedrockLauncher::instance();
        if (bedrock->isInstalled()) {
            const BedrockLauncher::BedrockInfo &info = bedrock->info();
            m_installedVersion = info.version.isEmpty() ? info.name : info.version;
        } else {
            m_installedVersion.clear();
        }
    }

    rebuildCards();
}

void BedrockInstanceSelectPage::onManagerChanged()
{
    if (isVisible())
        refresh();
}

void BedrockInstanceSelectPage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = static_cast<int>(mode);
    ContentViewSwitch::savePersisted("bedrock_instance_select", mode);
    if (m_viewStack)
        m_viewStack->setCurrentIndex(mode == ContentViewSwitch::List ? 0 : 1);
    // 与 Java 版一致：切换到卡片视图时按当前容器宽度重建卡片，保证布局正确
    if (mode == ContentViewSwitch::Masonry)
        rebuildMasonry();
}

void BedrockInstanceSelectPage::clearMasonry()
{
    while (QLayoutItem *item = m_masonryLayout->takeAt(0))
        delete item;
}

void BedrockInstanceSelectPage::rebuildMasonry()
{
    clearMasonry();
    const QVector<BedrockInstance> list = BedrockInstanceManager::instance()->instances();
    for (const BedrockInstance &inst : list)
        addMasonryCard(inst);
    m_masonryContainer->updateGeometry();
    startAsyncSizeCalculation();
}

void BedrockInstanceSelectPage::rebuildCards()
{
    m_sizeLabels.clear();
    m_instanceListWidget->clear();
    clearMasonry();

    const QVector<BedrockInstance> list = BedrockInstanceManager::instance()->instances();
    for (const BedrockInstance &inst : list) {
        addListCard(inst, m_instanceListWidget);
        // 卡片视图卡片仅在卡片视图下构建（与 Java 版一致，避免隐藏态零宽布局错乱）
        if (m_viewMode == ContentViewSwitch::Masonry)
            addMasonryCard(inst);
    }

    // 空状态：强制显示提示页；否则切回当前视图
    if (list.isEmpty()) {
        m_viewStack->setCurrentIndex(2);
    } else {
        m_viewStack->setCurrentIndex(m_viewMode == ContentViewSwitch::List ? 0 : 1);
    }

    startAsyncSizeCalculation();
}

/* ── 列表式卡片（与 Java 版实例卡片一致） ── */
void BedrockInstanceSelectPage::addListCard(const BedrockInstance &inst, QListWidget *listWidget)
{
    QListWidgetItem *item = new QListWidgetItem(listWidget);
    item->setData(Qt::UserRole, inst.id);

    QWidget *cardWidget = new QWidget();
    cardWidget->setObjectName("modCardListItem");
    cardWidget->setProperty("cardRole", "container");
    cardWidget->setFixedHeight(76);
    cardWidget->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *mainLayout = new QHBoxLayout(cardWidget);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    // 图标：基岩版石块图标（白底青）
    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    QPixmap iconPix(48, 48);
    iconPix.fill(kBedrockAccent.lighter(175));
    {
        QPainter painter(&iconPix);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPixmap bed = IconHelper::loadColoredIcon(":/Images/Icons/edition_bedrock.svg",
                                                        QColor(Qt::white), 28).pixmap(28, 28);
        painter.drawPixmap((48 - 28) / 2, (48 - 28) / 2, bed);
        painter.end();
    }
    iconLabel->setPixmap(iconPix);
    mainLayout->addWidget(iconLabel);

    // 信息列
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QWidget *nameRow = new QWidget();
    QHBoxLayout *nameLayout = new QHBoxLayout(nameRow);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(8);

    QLabel *nameLabel = new QLabel(inst.name);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    nameLayout->addWidget(nameLabel);

    if (inst.id == BedrockInstanceManager::instance()->activeInstanceId()) {
        QLabel *activeChip = new QLabel(tr("当前使用中"));
        activeChip->setObjectName("modChip");
        activeChip->setProperty("cardRole", "chip");
        activeChip->setStyleSheet("color:#1FA78F;");
        nameLayout->addWidget(activeChip);
    }
    nameLayout->addStretch();
    infoLayout->addWidget(nameRow);

    // 标签行
    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    auto addChip = [&](const QString &text) {
        QLabel *chip = new QLabel(text);
        chip->setObjectName("modChip");
        chip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(chip);
    };

    if (!inst.version.isEmpty())
        addChip(inst.version);

    const QString lastPlayedStr = inst.lastPlayed.isValid()
        ? inst.lastPlayed.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        : tr("从未游玩");
    addChip(lastPlayedStr);

    QLabel *sizeChip = new QLabel(tr("计算中..."));
    sizeChip->setObjectName("modChip");
    sizeChip->setProperty("cardRole", "chip");
    chipsLayout->addWidget(sizeChip);
    m_sizeLabels[inst.id].append(sizeChip);

    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);
    mainLayout->addLayout(infoLayout, 1);

    // 操作按钮
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    auto createBtn = [&](const QString &iconPath, const QString &tip, const QColor &color) -> QPushButton * {
        QPushButton *btn = new QPushButton();
        btn->setObjectName("modCardActionBtn");
        btn->setProperty("cardRole", "actionBtn");
        btn->setFixedSize(32, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, color, 18));
        btn->setIconSize(QSize(18, 18));
        return btn;
    };

    const QString id = inst.id;

    QPushButton *playBtn = createBtn(":/Images/Icons/play.svg", tr("启动游戏"), themeColor);
    connect(playBtn, &QPushButton::clicked, this, [this, id]() { launchInstance(id); });
    btnLayout->addWidget(playBtn);

    QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开实例文件夹"), themeColor);
    connect(folderBtn, &QPushButton::clicked, this, [this, id]() { openInstanceFolder(id); });
    btnLayout->addWidget(folderBtn);

    QPushButton *renameBtn = createBtn(":/Images/Icons/modify.svg", tr("重命名"), themeColor);
    connect(renameBtn, &QPushButton::clicked, this, [this, id]() { renameInstance(id); });
    btnLayout->addWidget(renameBtn);

    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除实例"), QColor("#F44336"));
    connect(deleteBtn, &QPushButton::clicked, this, [this, id]() { deleteInstance(id); });
    btnLayout->addWidget(deleteBtn);

    mainLayout->addLayout(btnLayout);

    listWidget->setItemWidget(item, cardWidget);
    item->setSizeHint(QSize(0, 76));
}

/* ── 瀑布流卡片（与 Java 版 contentCard 样式一致） ── */
QWidget *BedrockInstanceSelectPage::buildMasonryCard(const BedrockInstance &inst, QWidget *parent)
{
    const QColor c1 = kBedrockAccent;
    const QColor c2 = kBedrockAccent.darker(130);

    const int bannerH = 96;
    const int kCardW = 260;
    QWidget *card = new QWidget(parent);
    card->setObjectName("contentCard");
    card->setFixedWidth(kCardW);
    card->setCursor(Qt::PointingHandCursor);
    MasonryContentCard::applyShadow(card);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(1, 1, 1, 1);   // banner 内缩，露出卡片边框
    cardLayout->setSpacing(0);

    QLabel *banner = new QLabel(card);
    banner->setObjectName("contentCardBanner");
    banner->setFixedHeight(bannerH);
    banner->setPixmap(MasonryContentCard::makeBannerPixmap(c1, c2, kCardW - 2, bannerH));
    cardLayout->addWidget(banner);

    // 半压 logo：基岩版石块图标
    QLabel *logo = new QLabel(banner);
    logo->setObjectName("contentCardLogo");
    logo->setFixedSize(52, 52);
    logo->setAlignment(Qt::AlignCenter);
    logo->setPixmap(MasonryContentCard::makeLogoPixmap(c1, c2, QStringLiteral("B")));
    logo->move(kCardW - 2 - 52 - 12, bannerH - (52 - 10));
    auto *logoShadow = new QGraphicsDropShadowEffect(logo);
    logoShadow->setBlurRadius(4);
    logoShadow->setOffset(0, 1);
    logoShadow->setColor(QColor(0, 0, 0, 180));
    logo->setGraphicsEffect(logoShadow);
    logo->raise();

    QWidget *body = new QWidget(card);
    QVBoxLayout *bv = new QVBoxLayout(body);
    bv->setContentsMargins(12, 12, 12, 10);
    bv->setSpacing(6);

    QLabel *nameLabel = new QLabel(inst.name, body);
    nameLabel->setObjectName("contentCardName");
    bv->addWidget(nameLabel);

    // chips：版本 / 大小 / 上次游玩
    QWidget *chipsWidget = new QWidget(body);
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);
    auto addChip = [&](const QString &text) {
        QLabel *chip = new QLabel(text);
        chip->setObjectName("contentChip");
        chipsLayout->addWidget(chip);
    };
    if (!inst.version.isEmpty())
        addChip(inst.version);

    QLabel *sizeChip = new QLabel(tr("计算中..."));
    sizeChip->setObjectName("contentChip");
    m_sizeLabels[inst.id].append(sizeChip);
    chipsLayout->addWidget(sizeChip);

    const QString lastPlayedStr = inst.lastPlayed.isValid()
        ? inst.lastPlayed.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        : tr("从未游玩");
    addChip(lastPlayedStr);
    chipsLayout->addStretch();
    bv->addWidget(chipsWidget);

    // 操作按钮
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 4, 0, 0);
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton * {
        QPushButton *btn = new QPushButton();
        btn->setObjectName("contentCardActionBtn");
        btn->setFixedSize(30, 30);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 17));
        btn->setIconSize(QSize(17, 17));
        return btn;
    };

    const QString id = inst.id;
    QPushButton *playBtn = createBtn(":/Images/Icons/play.svg", tr("启动游戏"));
    connect(playBtn, &QPushButton::clicked, this, [this, id]() { launchInstance(id); });
    btnLayout->addWidget(playBtn);

    QPushButton *folderBtn = createBtn(":/Images/Icons/folder.svg", tr("打开实例文件夹"));
    connect(folderBtn, &QPushButton::clicked, this, [this, id]() { openInstanceFolder(id); });
    btnLayout->addWidget(folderBtn);

    QPushButton *renameBtn = createBtn(":/Images/Icons/modify.svg", tr("重命名"));
    connect(renameBtn, &QPushButton::clicked, this, [this, id]() { renameInstance(id); });
    btnLayout->addWidget(renameBtn);

    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除实例"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#F44336"), 17));
    connect(deleteBtn, &QPushButton::clicked, this, [this, id]() { deleteInstance(id); });
    btnLayout->addWidget(deleteBtn);
    btnLayout->addStretch();
    bv->addLayout(btnLayout);

    cardLayout->addWidget(body);

    // 点击卡片 → 启动
    card->setProperty("instanceId", inst.id);
    card->installEventFilter(this);

    return card;
}

void BedrockInstanceSelectPage::addMasonryCard(const BedrockInstance &inst)
{
    m_masonryLayout->addWidget(buildMasonryCard(inst, m_masonryContainer));
}

bool BedrockInstanceSelectPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            const QVariant idVar = watched->property("instanceId");
            if (idVar.isValid()) {
                launchInstance(idVar.toString());
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BedrockInstanceSelectPage::launchInstance(const QString &id)
{
    emit launchRequested(id);
}

void BedrockInstanceSelectPage::renameInstance(const QString &id)
{
    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    const BedrockInstance inst = mgr->instanceById(id);
    if (inst.id.isEmpty())
        return;

    bool ok = false;
    const QString newName = AppInputDialog::getText(this, tr("重命名实例"),
                                                    tr("实例名称"), QLineEdit::Normal,
                                                    inst.name, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == inst.name)
        return;

    if (mgr->renameInstance(id, newName.trimmed()))
        NotificationManager::showSuccess(this, tr("实例已重命名"));
}

void BedrockInstanceSelectPage::deleteInstance(const QString &id)
{
    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    const BedrockInstance inst = mgr->instanceById(id);
    if (inst.id.isEmpty())
        return;

    if (inst.id == mgr->activeInstanceId()) {
        NotificationManager::showError(this, tr("当前实例正在使用中，请先切换到其他实例再删除"));
        return;
    }

    AppMessageBox::StandardButton reply = AppMessageBox::question(this, tr("删除实例"),
        tr("确定要删除基岩版实例「%1」吗？\n其实例内所有游戏数据（存档、资源包等）将被删除，且不可恢复。")
            .arg(inst.name),
        AppMessageBox::Yes | AppMessageBox::No);
    if (reply != AppMessageBox::Yes)
        return;

    if (mgr->removeInstance(id))
        NotificationManager::showSuccess(this, tr("实例已删除"));
    else
        NotificationManager::showError(this, tr("删除实例失败"));
}

void BedrockInstanceSelectPage::openInstanceFolder(const QString &id)
{
    BedrockInstanceManager *mgr = BedrockInstanceManager::instance();
    const BedrockInstance inst = mgr->instanceById(id);
    if (inst.id.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(inst.dataDir));
}

void BedrockInstanceSelectPage::onCreateInstanceClicked()
{
    bool ok = false;
    const QString name = AppInputDialog::getText(this, tr("新建实例"),
                                                 tr("实例名称"), QLineEdit::Normal,
                                                 QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    BedrockInstanceManager::instance()->createInstance(name.trimmed());
    NotificationManager::showSuccess(this, tr("实例已创建，可在实例卡上点击「启动」开始使用"));
}

void BedrockInstanceSelectPage::onImportInstanceClicked()
{
    const QString srcDir = AppFileDialog::getExistingDirectory(this, tr("选择要导入的基岩版游戏数据目录"));
    if (srcDir.isEmpty())
        return;

    QDir src(srcDir);
    if (!src.exists()) {
        NotificationManager::showError(this, tr("所选目录不存在"));
        return;
    }

    bool ok = false;
    const QString name = AppInputDialog::getText(this, tr("导入游戏数据"),
                                                 tr("实例名称"), QLineEdit::Normal,
                                                 srcDir.split("/").last(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    BedrockInstanceManager::instance()->createInstance(name.trimmed(), srcDir);
    NotificationManager::showSuccess(this, tr("游戏数据已导入为新实例"));
}

void BedrockInstanceSelectPage::onOpenDataDirClicked()
{
    const QString dir = Platform::getBedrockDataDirectory() + QStringLiteral("/instances");
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

// ---------- 异步大小计算 ----------

QMap<QString, qint64> BedrockInstanceSelectPage::calculateSizesStatic(const QStringList &dirs)
{
    QMap<QString, qint64> result;
    for (const QString &path : dirs) {
        qint64 total = 0;
        QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            total += it.fileInfo().size();
        }
        result[path] = total;
    }
    return result;
}

void BedrockInstanceSelectPage::startAsyncSizeCalculation()
{
    const QVector<BedrockInstance> list = BedrockInstanceManager::instance()->instances();
    QStringList dirs;
    for (const BedrockInstance &inst : list) {
        if (QDir(inst.dataDir + QStringLiteral("/com.mojang")).exists())
            dirs << inst.dataDir;
    }
    if (dirs.isEmpty())
        return;
    m_sizeWatcher.setFuture(QtConcurrent::run(calculateSizesStatic, dirs));
}

void BedrockInstanceSelectPage::onAsyncSizesCalculated()
{
    const QMap<QString, qint64> sizes = m_sizeWatcher.future().result();
    const QVector<BedrockInstance> list = BedrockInstanceManager::instance()->instances();
    for (const BedrockInstance &inst : list) {
        const qint64 bytes = sizes.value(inst.dataDir, -1);
        if (bytes < 0)
            continue;
        QString sizeStr;
        if (bytes >= 1024 * 1024)
            sizeStr = QString("%1 MB").arg(bytes / (1024 * 1024));
        else if (bytes >= 1024)
            sizeStr = QString("%1 KB").arg(bytes / 1024);
        else
            sizeStr = QString("%1 B").arg(bytes);

        const QList<QLabel *> labels = m_sizeLabels.value(inst.id);
        for (QLabel *label : labels) {
            if (label)
                label->setText(sizeStr);
        }
    }
}

QIcon BedrockInstanceSelectPage::loadColoredIcon(const QString &path, const QColor &color) const
{
    return IconHelper::loadColoredIcon(path, color, 16);
}
