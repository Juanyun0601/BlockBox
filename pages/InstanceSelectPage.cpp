/**
 * @file   InstanceSelectPage.cpp
 * @brief  实例选择页面实现
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "InstanceSelectPage.h"

#include <QCache>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include "components/AppFileDialog.h"
#include "components/AppInputDialog.h"
#include "components/MasonryContentCard.h"
#include "../layouts/MasonryLayout.h"
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMenu>
#include "components/AppMessageBox.h"
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QSequentialAnimationGroup>
#include <QProcess>
#include <QUrl>

#include "components/NotificationManager.h"
#include <QPixmap>
#include <QProgressBar>
#include <QSpacerItem>
#include <QStyle>
#include <QTimer>


#include "utils/GameLauncher.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"

namespace {
// 新建分类时的名称预设，用户可从下拉框快速选择或自行输入
QStringList categoryNamePresets()
{
    return {
        QObject::tr("原版"),
        QObject::tr("生存"),
        QObject::tr("创造"),
        QObject::tr("模组"),
        QObject::tr("整合包"),
        QObject::tr("服务器"),
        QObject::tr("小游戏"),
        QObject::tr("实验")
    };
}
} // namespace

InstanceSelectPage::InstanceSelectPage(QWidget *parent) : QWidget(parent)
    , m_leftWidget(nullptr)
    , m_folderHighlight(nullptr)
    , m_iconNormalColor("#666666")
    , m_iconSelectedColor("#ffffff")
    , m_bindFolderBtn(nullptr)
    , m_folderButtonsWidget(nullptr)
    , m_folderButtonsLayout(nullptr)
    , m_categoryButtonsWidget(nullptr)
    , m_categoryButtonsLayout(nullptr)
    , m_allCategoryBtn(nullptr)
    , m_addCategoryBtn(nullptr)
    , m_categoryHighlight(nullptr)
{
    initUI();
    initLauncher();

    connect(&m_sizeWatcher, &QFutureWatcher<QMap<QString, qint64>>::finished,
            this, &InstanceSelectPage::onAsyncSizesCalculated);
}

InstanceSelectPage::~InstanceSelectPage()
{
}

void InstanceSelectPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_instancesLoaded)
    {
        m_instancesLoaded = true;
        initInstanceFolders();
        loadInstances();
        updateInstanceList();
    }
    // 每次显示时重新贴靠气泡几何（页面切换动画期间尺寸可能变化）
    QTimer::singleShot(0, this, [this]() { updateLeftBubbleGeometry(); });
}

void InstanceSelectPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLeftBubbleGeometry();
}

void InstanceSelectPage::updateLeftBubbleGeometry()
{
    if (!m_leftWidget)
    {
        return;
    }
    // 页面左上角悬浮：x=间距，y=间距，宽度固定，高度 = 页面高 - 上下间距
    const int x = kBubbleMargin;
    const int y = kBubbleMargin;
    const int h = qMax(120, height() - 2 * kBubbleMargin);
    m_leftWidget->setGeometry(x, y, kBubbleWidth, h);
    m_leftWidget->raise();
}

void InstanceSelectPage::initUI()
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Initialize left sidebar and right main bar
    initLeftSidebar();
    initRightMainBar();

    // Set minimum sizes for left and right sections
    m_leftWidget = new QWidget(this);
    m_leftWidget->setObjectName("instanceLeftSidebar");
    m_leftWidget->setLayout(m_leftLayout);
    m_leftWidget->setAttribute(Qt::WA_StyledBackground);
    m_leftWidget->setFixedWidth(kBubbleWidth);


    // 悬浮气泡外阴影
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
    // 不调用 lower()——否则会被布局内的 spacer/separator 完全遮挡
    // 高亮条只做视觉指示，绝不能拦截鼠标事件
    m_folderHighlight->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // 分类区滑动高亮背景
    m_categoryHighlight = new QWidget(m_leftWidget);
    m_categoryHighlight->setObjectName("instanceFolderHighlight");
    m_categoryHighlight->setVisible(false);
    m_categoryHighlight->setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void InstanceSelectPage::initLeftSidebar()
{
    m_leftLayout = new QVBoxLayout();
    m_leftLayout->setContentsMargins(8, 12, 8, 12);
    m_leftLayout->setSpacing(6);

    // --- Folders group title ---
    QLabel *foldersTitle = new QLabel(tr("实例文件夹"));
    foldersTitle->setObjectName("instanceSidebarSectionTitle");
    m_leftLayout->addWidget(foldersTitle);

    // --- Folder buttons container ---
    m_folderButtonsWidget = new QWidget();
    m_folderButtonsLayout = new QVBoxLayout(m_folderButtonsWidget);
    m_folderButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_folderButtonsLayout->setSpacing(6);
    m_leftLayout->addWidget(m_folderButtonsWidget);

    m_leftLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding));

    // --- Category section: title + 新建按钮 ---
    QWidget *categoryHeaderRow = new QWidget();
    QHBoxLayout *categoryHeaderLayout = new QHBoxLayout(categoryHeaderRow);
    categoryHeaderLayout->setContentsMargins(0, 0, 0, 0);
    categoryHeaderLayout->setSpacing(4);

    QLabel *categoryTitle = new QLabel(tr("实例分类"));
    categoryTitle->setObjectName("instanceSidebarSectionTitle");
    categoryHeaderLayout->addWidget(categoryTitle);
    categoryHeaderLayout->addStretch();

    m_addCategoryBtn = new QPushButton("+");
    m_addCategoryBtn->setObjectName("instanceSidebarAddBtn");
    m_addCategoryBtn->setFixedSize(18, 18);
    m_addCategoryBtn->setCursor(Qt::PointingHandCursor);
    m_addCategoryBtn->setToolTip(tr("新建分类"));
    m_addCategoryBtn->setFocusPolicy(Qt::NoFocus);
    categoryHeaderLayout->addWidget(m_addCategoryBtn);
    m_leftLayout->addWidget(categoryHeaderRow);

    // --- Category buttons container ---
    m_categoryButtonsWidget = new QWidget();
    m_categoryButtonsLayout = new QVBoxLayout(m_categoryButtonsWidget);
    m_categoryButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_categoryButtonsLayout->setSpacing(6);
    m_leftLayout->addWidget(m_categoryButtonsWidget);

    // --- Bottom separator above bind button ---
    QFrame *bottomSeparator = new QFrame();
    bottomSeparator->setObjectName("instanceSidebarSeparator");
    bottomSeparator->setFixedHeight(1);
    m_leftLayout->addWidget(bottomSeparator);

    m_bindFolderBtn = new QPushButton(tr("绑定文件夹"));
    m_bindFolderBtn->setObjectName("instanceBindFolderBtn");
    m_bindFolderBtn->setFixedHeight(34);
    m_bindFolderBtn->setIconSize(QSize(16, 16));
    m_bindFolderBtn->setIcon(loadColoredIcon(":/Images/Icons/nav_folder_plus.svg", m_iconNormalColor));
    m_bindFolderBtn->setCursor(Qt::PointingHandCursor);
    m_leftLayout->addWidget(m_bindFolderBtn);

    connect(m_bindFolderBtn, &QPushButton::clicked, this, &InstanceSelectPage::onBindFolderClicked);
    connect(m_addCategoryBtn, &QPushButton::clicked, this, &InstanceSelectPage::onAddCategoryClicked);

    initCategorySection();
}

void InstanceSelectPage::initRightMainBar()
{
    m_rightLayout = new QVBoxLayout();
    m_rightLayout->setContentsMargins(10, 10, 10, 10);
    m_rightLayout->setSpacing(10);

    // Top section
    m_topLayout = new QHBoxLayout();
    m_topLayout->setContentsMargins(0, 0, 0, 10);
    m_topLayout->setSpacing(10);

    m_viewSwitch = new ContentViewSwitch(this);
    m_viewSwitch->setViewMode(ContentViewSwitch::loadPersisted("instance_select", ContentViewSwitch::List));
    m_viewMode = static_cast<int>(m_viewSwitch->viewMode());
    connect(m_viewSwitch, &ContentViewSwitch::viewModeChanged,
            this, &InstanceSelectPage::onViewModeChanged);
    m_topLayout->addWidget(m_viewSwitch);

    m_topLayout->addStretch();
    
    // Action buttons
    m_installNewInstanceBtn = new QPushButton(tr("安装新实例"));
    m_installNewInstanceBtn->setObjectName("instanceActionBtn");
    m_downloadModpackBtn = new QPushButton(tr("下载整合包"));
    m_downloadModpackBtn->setObjectName("instanceActionBtn");
    m_importModpackBtn = new QPushButton(tr("导入整合包"));
    m_importModpackBtn->setObjectName("instanceActionBtn");
    
    m_topLayout->addWidget(m_installNewInstanceBtn);
    m_topLayout->addWidget(m_downloadModpackBtn);
    m_topLayout->addWidget(m_importModpackBtn);
    
    m_rightLayout->addLayout(m_topLayout);
    
    // Separator between top section and instance list
    QFrame *separator = new QFrame();
    separator->setObjectName("instanceListSeparator");
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    m_rightLayout->addWidget(separator);
    
    // Instance list area
    m_instanceScrollArea = new QScrollArea();
    m_instanceScrollArea->setObjectName("instanceScrollArea");
    m_instanceScrollArea->setWidgetResizable(true);
    
    m_instanceContainer = new QWidget();
    m_instanceContainer->setObjectName("instanceContainer");
    
    // Create list widget for instance cards
    m_instanceListWidget = new QListWidget();
    m_instanceListWidget->setObjectName("instanceListWidget");
    m_instanceListWidget->setProperty("flatContainer", true);
    m_instanceListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_instanceListWidget->setAlternatingRowColors(false);
    m_instanceListWidget->setUniformItemSizes(true);
    m_instanceListWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_instanceListWidget->setMovement(QListView::Static);
    m_instanceListWidget->setViewMode(QListView::ListMode);
    m_instanceListWidget->setIconSize(QSize(64, 64));
    m_instanceListWidget->setSpacing(8);
    
    // 瀑布流容器（页 1）
    m_masonryContainer = new QWidget();
    m_masonryContainer->setObjectName("instanceMasonryContainer");
    m_masonryLayout = new MasonryLayout(m_masonryContainer, 0, 12, 12);
    m_masonryLayout->setContentsMargins(0, 0, 0, 0);

    // 双视图：页0=列表式 QListWidget，页1=瀑布流
    m_viewStack = new QStackedWidget();
    m_viewStack->addWidget(m_instanceListWidget);
    m_viewStack->addWidget(m_masonryContainer);
    m_viewStack->setCurrentIndex(m_viewMode == ContentViewSwitch::Masonry ? 1 : 0);

    QVBoxLayout *scrollLayout = new QVBoxLayout(m_instanceContainer);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->addWidget(m_viewStack);
    
    m_instanceScrollArea->setWidget(m_instanceContainer);
    m_rightLayout->addWidget(m_instanceScrollArea);
    
    // Connect signals and slots
    connect(m_instanceListWidget, &QListWidget::itemClicked, this, &InstanceSelectPage::onInstanceItemClicked);
    m_instanceListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_instanceListWidget, &QListWidget::customContextMenuRequested, this, &InstanceSelectPage::onInstanceContextMenu);
    connect(m_installNewInstanceBtn, &QPushButton::clicked, this, &InstanceSelectPage::onInstallNewInstanceClicked);
    connect(m_downloadModpackBtn, &QPushButton::clicked, this, &InstanceSelectPage::onDownloadModpackClicked);
    connect(m_importModpackBtn, &QPushButton::clicked, this, &InstanceSelectPage::onImportModpackClicked);
}

void InstanceSelectPage::initInstanceFolders()
{
    // Clear existing folder buttons
    for (QPushButton *btn : m_instanceFolderButtons) {
        m_folderButtonsLayout->removeWidget(btn);
        delete btn;
    }
    m_instanceFolderButtons.clear();

    // Load instance folders from SettingsManager
    QList<InstanceFolderInfo> instanceFolders = SettingsManager::instance()->getInstanceFolders();

    if (instanceFolders.isEmpty()) {
        // Default .minecraft folder in the same directory as the launcher
        QString defaultMinecraftPath = QCoreApplication::applicationDirPath() + "/.minecraft";

        // Check if default folder exists, if not create it
        QDir defaultDir(defaultMinecraftPath);
        if (!defaultDir.exists()) {
            defaultDir.mkpath(".");
        }

        // Save default folder to SettingsManager
        SettingsManager::instance()->addInstanceFolder(tr("默认 .minecraft"), defaultMinecraftPath, true);

        QPushButton *defaultBtn = createFolderNavButton(tr("默认 .minecraft"), defaultMinecraftPath, true);
        m_instanceFolderButtons.append(defaultBtn);
        m_currentFolderPath = defaultMinecraftPath;
    } else {
        // Add each instance folder as a button
        bool firstButton = true;
        for (const InstanceFolderInfo &folder : instanceFolders) {
            QPushButton *btn = createFolderNavButton(folder.name, folder.path, firstButton);
            if (firstButton) {
                m_currentFolderPath = folder.path;
                firstButton = false;
            }
            m_instanceFolderButtons.append(btn);
        }
    }

    // Add all folder buttons to layout
    for (QPushButton *btn : m_instanceFolderButtons) {
        m_folderButtonsLayout->addWidget(btn);
    }

    // 刷新分类按钮（分类可能已变化）
    rebuildCategoryButtons();

    // 入场动画：与其他子导航栏一致的交错淡入
    animateFolderEntrance();

    // 初始化滑动高亮位置（延迟到下次事件循环以确保 geometry 就绪）
    if (!m_instanceFolderButtons.isEmpty()) {
        QPushButton *checkedBtn = nullptr;
        for (QPushButton *btn : m_instanceFolderButtons) {
            if (btn->isChecked()) {
                checkedBtn = btn;
                break;
            }
        }
        if (checkedBtn) {
            QTimer::singleShot(0, this, [this, checkedBtn]() {
                slideHighlightTo(checkedBtn);
            });
        }
    }
}

QPushButton *InstanceSelectPage::createFolderNavButton(const QString &name, const QString &path, bool checked)
{
    QPushButton *btn = new QPushButton(name);
    btn->setObjectName("instanceFolderNavBtn");
    btn->setFixedHeight(34);
    btn->setCheckable(true);
    btn->setChecked(checked);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setIconSize(QSize(16, 16));
    btn->setProperty("folderPath", path);
    btn->setProperty("iconPath", ":/Images/Icons/nav_folder.svg");
    btn->setIcon(loadColoredIcon(":/Images/Icons/nav_folder.svg",
                                 checked ? m_iconSelectedColor : m_iconNormalColor));

    connect(btn, &QPushButton::clicked, this, [this, btn]() {
        setSelectedFolder(btn);
    });

    return btn;
}

QString InstanceSelectPage::folderDisplayName(const QString &path)
{
    QString name = QDir(path).dirName();
    // 如果是 .minecraft 文件夹，使用父目录名称作为显示名
    if (name == QStringLiteral(".minecraft")) {
        QDir parentDir(path);
        if (parentDir.cdUp())
            name = parentDir.dirName();
    }
    return name;
}

InstanceInfo InstanceSelectPage::getInstanceInfo(const QFileInfo &instanceInfo)
{
    QString instancePath = instanceInfo.absoluteFilePath();
    
    // 检查缓存
    InstanceInfo *cachedInfo = m_instanceInfoCache[instancePath];
    QFile versionJsonFile(instancePath + "/" + instanceInfo.fileName() + ".json");
    
    // 如果缓存存在且文件未修改，返回缓存的信息
    if (cachedInfo && versionJsonFile.exists()) {
        QFileInfo fileInfo(versionJsonFile.fileName());
        QDateTime fileModified = fileInfo.lastModified();
        if (fileModified <= cachedInfo->lastModified) {
            return *cachedInfo;
        }
    }
    
    // 缓存不存在或文件已修改，重新读取
    InstanceInfo info;
    info.loaderType = tr("未知");
    info.versionType = tr("未知");
    info.versionNumber = instanceInfo.fileName();
    info.lastModified = QDateTime::currentDateTime();
    
    if (versionJsonFile.open(QIODevice::ReadOnly)) {
        QByteArray jsonData = versionJsonFile.readAll();
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData, &parseError);
        
        if (parseError.error == QJsonParseError::NoError) {
            QJsonObject rootObj = jsonDoc.object();
            
            // Get version type
            if (rootObj.contains("type")) {
                QString type = rootObj["type"].toString();
                if (type == "release") {
                    info.versionType = tr("正式版");
                } else if (type == "snapshot") {
                    info.versionType = tr("快照版");
                } else {
                    info.versionType = type;
                }
            }
            
            // Check for loader type (Fabric, Forge, etc.)
            if (rootObj.contains("fabricLoader")) {
                info.loaderType = tr("Fabric");
            } else if (rootObj.contains("forge")) {
                info.loaderType = tr("Forge");
            } else if (rootObj.contains("quiltLoader")) {
                info.loaderType = tr("Quilt");
            }
            
            // Get actual version number if available
            // Prefer inheritsFrom/clientVersion for clean MC version display
            // (id field for modded instances includes loader info, e.g. "1.20.1-forge-47.2.0")
            if (rootObj.contains("inheritsFrom")) {
                info.versionNumber = rootObj["inheritsFrom"].toString();
            } else if (rootObj.contains("clientVersion")) {
                info.versionNumber = rootObj["clientVersion"].toString();
            } else if (rootObj.contains("id")) {
                info.versionNumber = rootObj["id"].toString();
            }
        }
        
        versionJsonFile.close();
    }
    
    // 延迟计算文件夹大小（异步），避免主线程阻塞
    info.sizeBytes = -1; // -1 表示正在计算中
    
    // 读取自定义实例名称与图标（实例设置中可修改）
    SettingsManager *settings = SettingsManager::instance();
    info.displayName = settings->getProperty("instance/" + instancePath + "/displayName").toString();
    info.iconPath = settings->getProperty("instance/" + instancePath + "/iconPath").toString();
    if (!info.iconPath.isEmpty() && !QFile::exists(info.iconPath))
        info.iconPath.clear();
    
    // 读取 lastPlayed：使用版本JSON文件的最后修改时间作为代理
    QFileInfo jsonFileInfo(instancePath + "/" + instanceInfo.fileName() + ".json");
    info.lastPlayed = jsonFileInfo.lastModified();
    
    // 更新缓存，增大缓存预算避免过早淘汰
    m_instanceInfoCache.insert(instancePath, new InstanceInfo(info), 10000);
    
    return info;
}

void InstanceSelectPage::loadInstances()
{
    // 检查文件夹是否有变化
    QDir instancesDir(m_currentFolderPath + "/versions");
    if (!instancesDir.exists()) {
        m_instances.clear();
        return;
    }
    
    // 检查文件夹的最后修改时间
    QFileInfo dirInfo(instancesDir.path());
    QDateTime folderModified = dirInfo.lastModified();
    if (m_folderLastModifiedMap.contains(m_currentFolderPath) && 
        folderModified == m_folderLastModifiedMap[m_currentFolderPath] &&
        m_folderInstancesMap.contains(m_currentFolderPath)) {
        // 文件夹未修改，使用缓存的实例列表
        m_instances = m_folderInstancesMap[m_currentFolderPath];
        return;
    }
    
    // 文件夹已修改，重新加载
    m_instances.clear();
    
    // 只遍历一级目录，不遍历嵌套子目录
    QDirIterator it(instancesDir.path(), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        QString instancePath = it.next();
        QFileInfo instanceInfo(instancePath);
        QString instanceName = instanceInfo.fileName();
        
        // 排除常见的非实例目录
        if (instanceName.startsWith(".") || // 隐藏目录
            instanceName == "assets" || // 资源目录
            instanceName == "libraries" || // 库目录
            instanceName == "logs" || // 日志目录
            instanceName == "crash-reports" || // 崩溃报告目录
            instanceName == "saves" || // 存档目录
            instanceName == "mods" || // 模组目录
            instanceName == "config" || // 配置目录
            instanceName == "resourcepacks") { // 资源包目录
            continue;
        }
        
        // Check if this is a valid Minecraft instance
        // 参考 HMCL 做法：只需 JSON 文件存在即可识别为实例
        // Forge 等加载器通过 inheritsFrom 继承原版 jar，不需要单独 jar 文件
        bool isValidInstance = false;
        QFile versionJsonFile(instancePath + "/" + instanceName + ".json");
        
        if (versionJsonFile.exists())
        {
            isValidInstance = true;
        }
        else
        {
            // 回退：版本隔离时，JSON 文件名可能与目录名不同
            // 扫描目录中的 .json 文件，检查是否包含有效的 "id" 字段
            QDirIterator jsonIt(instancePath, {"*.json"}, QDir::Files, QDirIterator::NoIteratorFlags);
            while (jsonIt.hasNext())
            {
                jsonIt.next();
                QFile fallbackFile(jsonIt.filePath());
                if (fallbackFile.open(QIODevice::ReadOnly))
                {
                    QByteArray data = fallbackFile.readAll();
                    fallbackFile.close();
                    
                    QJsonParseError parseError;
                    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
                    if (parseError.error == QJsonParseError::NoError && doc.isObject())
                    {
                        QJsonObject obj = doc.object();
                        if (obj.contains("id") && obj["id"].isString() && !obj["id"].toString().isEmpty())
                        {
                            isValidInstance = true;
                            break;
                        }
                    }
                }
            }
        }
        
        if (isValidInstance)
        {
            m_instances.append(instanceInfo);
        }
    }
    
    // 更新缓存
    m_folderInstancesMap[m_currentFolderPath] = m_instances;
    m_folderLastModifiedMap[m_currentFolderPath] = folderModified;
}

void InstanceSelectPage::rebuildInstanceCards()
{
    m_instanceSizeLabels.clear();
    m_instanceListWidget->clear();
    // 清空瀑布流容器
    while (QLayoutItem *item = m_masonryLayout->takeAt(0))
        delete item;

    for (const QFileInfo &instanceInfo : m_instances) {
        if (!matchesCategory(instanceInfo.absoluteFilePath()))
            continue;
        addInstanceCard(instanceInfo, m_instanceListWidget);
        if (m_viewMode == ContentViewSwitch::Masonry)
            addMasonryInstanceCard(instanceInfo);
    }
    startAsyncSizeCalculation();
}

/* 瀑布流实例卡片（contentCard 样式） */
QWidget *InstanceSelectPage::buildMasonryInstanceCard(const QFileInfo &instanceInfo, QWidget *parent)
{
    InstanceInfo data = getInstanceInfo(instanceInfo);

    // loaderType 色板（与列表卡一致）
    QColor loaderColor = (data.loaderType == "Forge") ? QColor("#4CAF50") :
                         (data.loaderType == "Fabric") ? QColor("#FF9800") :
                         (data.loaderType == "Quilt") ? QColor("#9C27B0") :
                         (data.loaderType == "NeoForge") ? QColor("#E91E63") : QColor("#9E9E9E");
    const QColor c1 = loaderColor;
    const QColor c2 = loaderColor.darker(130);

    const int bannerH = 96;
    const int kCardW = 260;
    auto *card = new QWidget(parent);
    card->setObjectName("contentCard");
    card->setProperty("flatCard", true);
    card->setFixedWidth(kCardW);
    card->setCursor(Qt::PointingHandCursor);
    MasonryContentCard::applyShadow(card);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(1, 1, 1, 1);   // banner 内缩，露出卡片边框
    cardLayout->setSpacing(0);

    auto *banner = new QLabel(card);
    banner->setObjectName("contentCardBanner");
    banner->setFixedHeight(bannerH);
    banner->setPixmap(MasonryContentCard::makeBannerPixmap(c1, c2, kCardW - 2, bannerH));
    cardLayout->addWidget(banner);

    // 半压 logo：优先使用自定义图标，否则加载器首字母
    auto *logo = new QLabel(banner);
    logo->setObjectName("contentCardLogo");
    logo->setFixedSize(52, 52);
    logo->setAlignment(Qt::AlignCenter);
    if (!data.iconPath.isEmpty() && QFile::exists(data.iconPath))
    {
        QPixmap iconPm(data.iconPath);
        if (!iconPm.isNull())
        {
            logo->setPixmap(iconPm.scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        else
        {
            const QString letter = data.loaderType.isEmpty() ? QStringLiteral("?")
                                   : data.loaderType.left(1).toUpper();
            logo->setPixmap(MasonryContentCard::makeLogoPixmap(c1, c2, letter));
        }
    }
    else
    {
        const QString letter = data.loaderType.isEmpty() ? QStringLiteral("?")
                               : data.loaderType.left(1).toUpper();
        logo->setPixmap(MasonryContentCard::makeLogoPixmap(c1, c2, letter));
    }
    logo->move(kCardW - 2 - 52 - 12, bannerH - (52 - 10));
    auto *logoShadow = new QGraphicsDropShadowEffect(logo);
    logoShadow->setBlurRadius(4);
    logoShadow->setOffset(0, 1);
    logoShadow->setColor(QColor(0, 0, 0, 180));
    logo->setGraphicsEffect(logoShadow);
    logo->raise();

    auto *body = new QWidget(card);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(12, 12, 12, 10);
    bv->setSpacing(6);

    const QString displayName = data.displayName.isEmpty()
        ? instanceInfo.fileName() : data.displayName;
    auto *nameLabel = new QLabel(displayName, body);
    nameLabel->setObjectName("contentCardName");
    bv->addWidget(nameLabel);

    // chips：版本 / 加载器 / 大小
    auto *chipsWidget = new QWidget(body);
    auto *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);
    auto addChip = [&](const QString &text) {
        auto *chip = new QLabel(text);
        chip->setObjectName("contentChip");
        chipsLayout->addWidget(chip);
    };
    if (!data.versionNumber.isEmpty()) addChip(data.versionNumber);
    if (!data.loaderType.isEmpty()) addChip(data.loaderType);
    QLabel *sizeChip = new QLabel(tr("计算中..."));
    sizeChip->setObjectName("contentChip");
    sizeChip->setStyleSheet(sizeChip->styleSheet());
    if (data.sizeBytes < 0)
    {
        sizeChip->setText(tr("计算中..."));
        m_instanceSizeLabels[instanceInfo.absoluteFilePath()] = sizeChip;
    }
    else if (data.sizeBytes >= 1024 * 1024)
        sizeChip->setText(QString("%1 MB").arg(data.sizeBytes / (1024 * 1024)));
    else if (data.sizeBytes >= 1024)
        sizeChip->setText(QString("%1 KB").arg(data.sizeBytes / 1024));
    else
        sizeChip->setText(QString("%1 B").arg(data.sizeBytes));
    chipsLayout->addWidget(sizeChip);
    const QString category = SettingsManager::instance()->getInstanceCategory(instanceInfo.absoluteFilePath());
    if (!category.isEmpty()) {
        QLabel *categoryChip = new QLabel(category);
        categoryChip->setObjectName("contentChip");
        categoryChip->setProperty("categoryChip", true);
        chipsLayout->addWidget(categoryChip);
    }
    chipsLayout->addStretch();
    bv->addWidget(chipsWidget);

    // 上次游玩
    auto *metaLabel = new QLabel(body);
    metaLabel->setObjectName("contentCardDesc");
    QString lastPlayedStr = data.lastPlayed.isValid()
        ? data.lastPlayed.toString("yyyy-MM-dd HH:mm") : tr("从未游玩");
    metaLabel->setText(lastPlayedStr);
    bv->addWidget(metaLabel);
    bv->addStretch();

    // 操作按钮：启动 / 设置
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 4, 0, 0);
    QColor themeColor(ThemeManager::instance()->currentThemeColor());
    auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton* {
        auto *btn = new QPushButton();
        btn->setObjectName("contentCardActionBtn");
        btn->setFixedSize(30, 30);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 17));
        btn->setIconSize(QSize(17, 17));
        return btn;
    };

    QString instancePath = instanceInfo.absoluteFilePath();
    auto *playBtn = createBtn(":/Images/Icons/play.svg", tr("启动游戏"));
    connect(playBtn, &QPushButton::clicked, [this, instancePath]() {
        onLaunchGameClicked(instancePath);
    });
    btnLayout->addWidget(playBtn);

    auto *settingsBtn = createBtn(":/Images/Icons/instance_settings.svg", tr("实例设置"));
    connect(settingsBtn, &QPushButton::clicked, [this, instancePath]() {
        emit instanceSelected(instancePath);
    });
    btnLayout->addWidget(settingsBtn);
    btnLayout->addStretch();
    bv->addLayout(btnLayout);

    cardLayout->addWidget(body);

    // 点击卡片 → 启动
    card->setProperty("instancePath", instancePath);
    card->installEventFilter(this);

    return card;
}

void InstanceSelectPage::addMasonryInstanceCard(const QFileInfo &instanceInfo)
{
    m_masonryLayout->addWidget(buildMasonryInstanceCard(instanceInfo, m_masonryContainer));
}

void InstanceSelectPage::rebuildMasonryCards()
{
    while (QLayoutItem *item = m_masonryLayout->takeAt(0))
        delete item;
    for (const QFileInfo &instanceInfo : m_instances) {
        if (!matchesCategory(instanceInfo.absoluteFilePath()))
            continue;
        addMasonryInstanceCard(instanceInfo);
    }
    startAsyncSizeCalculation();
}

void InstanceSelectPage::onViewModeChanged(ContentViewSwitch::ViewMode mode)
{
    m_viewMode = static_cast<int>(mode);
    ContentViewSwitch::savePersisted("instance_select", mode);
    if (m_viewStack)
        m_viewStack->setCurrentIndex(mode == ContentViewSwitch::List ? 0 : 1);
    if (mode == ContentViewSwitch::Masonry)
        rebuildMasonryCards();
}

/* 瀑布流卡片点击 → 启动；右键 → 分类/操作菜单 */
bool InstanceSelectPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        QVariant pathVar = watched->property("instancePath");
        if (!pathVar.isValid())
            return QWidget::eventFilter(watched, event);

        if (mouseEvent->button() == Qt::LeftButton)
        {
            onLaunchGameClicked(pathVar.toString());
            return true;
        }
        if (mouseEvent->button() == Qt::RightButton)
        {
            QMenu menu(this);
            menu.setStyleSheet(this->styleSheet());
            QAction *launchAction = menu.addAction(tr("启动游戏"));
            QAction *settingsAction = menu.addAction(tr("实例设置"));
            menu.addSeparator();
            QMenu *categoryMenu = menu.addMenu(tr("移动分类"));
            buildCategoryMenu(categoryMenu, pathVar.toString());
            menu.addSeparator();
            QAction *deleteAction = menu.addAction(tr("删除实例"));
            deleteAction->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#F44336"), 16));

            QAction *chosen = menu.exec(static_cast<QWidget *>(watched)->mapToGlobal(mouseEvent->pos()));
            if (chosen == launchAction) {
                onLaunchGameClicked(pathVar.toString());
            } else if (chosen == settingsAction) {
                emit instanceSelected(pathVar.toString());
                emit backToMainRequested();
            } else if (chosen == deleteAction) {
                onDeleteInstanceClicked(pathVar.toString());
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}



void InstanceSelectPage::updateInstanceList()
{
    rebuildInstanceCards();
}

void InstanceSelectPage::addInstanceCard(const QFileInfo &instanceInfo, QListWidget *listWidget)
{
    QListWidgetItem *item = new QListWidgetItem(listWidget);
    item->setData(Qt::UserRole, instanceInfo.absoluteFilePath());

    InstanceInfo instanceData = getInstanceInfo(instanceInfo);

    QWidget *cardWidget = new QWidget();
    cardWidget->setObjectName("modCardListItem");
    cardWidget->setProperty("cardRole", "container");
    cardWidget->setProperty("flatCard", true);
    cardWidget->setFixedHeight(76);
    cardWidget->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *mainLayout = new QHBoxLayout(cardWidget);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(14);

    QLabel *iconLabel = new QLabel();
    iconLabel->setObjectName("modCardIcon");
    iconLabel->setProperty("cardRole", "icon");
    iconLabel->setFixedSize(48, 48);
    iconLabel->setAlignment(Qt::AlignCenter);
    QPixmap placeholderIcon(48, 48);
    QColor iconColor = (instanceData.loaderType == "Forge") ? QColor("#4CAF50") :
                       (instanceData.loaderType == "Fabric") ? QColor("#FF9800") :
                       (instanceData.loaderType == "Quilt") ? QColor("#9C27B0") :
                       (instanceData.loaderType == "NeoForge") ? QColor("#E91E63") : QColor("#9E9E9E");
    placeholderIcon.fill(iconColor.lighter(180));
    {
        QPainter painter(&placeholderIcon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::white);
        QFont font;
        font.setBold(true);
        font.setPixelSize(24);
        painter.setFont(font);
        QString letter = instanceData.loaderType.isEmpty() ? "?" : instanceData.loaderType.left(1).toUpper();
        painter.drawText(placeholderIcon.rect(), Qt::AlignCenter, letter);
        painter.end();
    }
    iconLabel->setPixmap(placeholderIcon);
    // 支持自定义图标
    if (!instanceData.iconPath.isEmpty() && QFile::exists(instanceData.iconPath))
    {
        QPixmap customIcon(instanceData.iconPath);
        if (!customIcon.isNull())
        {
            iconLabel->setPixmap(customIcon.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
    mainLayout->addWidget(iconLabel);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(6);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    const QString displayName = instanceData.displayName.isEmpty()
        ? instanceInfo.fileName() : instanceData.displayName;
    QLabel *nameLabel = new QLabel(displayName);
    nameLabel->setObjectName("modCardName");
    nameLabel->setProperty("cardRole", "name");
    infoLayout->addWidget(nameLabel);

    QWidget *chipsWidget = new QWidget();
    QHBoxLayout *chipsLayout = new QHBoxLayout(chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);

    if (!instanceData.versionNumber.isEmpty()) {
        QLabel *versionChip = new QLabel(instanceData.versionNumber);
        versionChip->setObjectName("modChip");
        versionChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(versionChip);
    }

    if (!instanceData.loaderType.isEmpty()) {
        QLabel *loaderChip = new QLabel(instanceData.loaderType);
        loaderChip->setObjectName("modChip");
        loaderChip->setProperty("cardRole", "chip");
        chipsLayout->addWidget(loaderChip);
    }

    QString lastPlayedStr = instanceData.lastPlayed.isValid()
        ? instanceData.lastPlayed.toString("yyyy-MM-dd HH:mm")
        : tr("从未游玩");
    QLabel *playedChip = new QLabel(lastPlayedStr);
    playedChip->setObjectName("modChip");
    playedChip->setProperty("cardRole", "chip");
    chipsLayout->addWidget(playedChip);

    QString sizeStr;
    if (instanceData.sizeBytes < 0)
        sizeStr = tr("计算中...");
    else if (instanceData.sizeBytes >= 1024 * 1024)
        sizeStr = QString("%1 MB").arg(instanceData.sizeBytes / (1024 * 1024));
    else if (instanceData.sizeBytes >= 1024)
        sizeStr = QString("%1 KB").arg(instanceData.sizeBytes / 1024);
    else
        sizeStr = QString("%1 B").arg(instanceData.sizeBytes);
    QLabel *sizeChip = new QLabel(sizeStr);
    sizeChip->setObjectName("modChip");
    sizeChip->setProperty("cardRole", "chip");
    chipsLayout->addWidget(sizeChip);

    if (instanceData.sizeBytes < 0)
        m_instanceSizeLabels[instanceInfo.absoluteFilePath()] = sizeChip;

    const QString category = SettingsManager::instance()->getInstanceCategory(instanceInfo.absoluteFilePath());
    if (!category.isEmpty()) {
        QLabel *categoryChip = new QLabel(category);
        categoryChip->setObjectName("modChip");
        categoryChip->setProperty("cardRole", "chip");
        categoryChip->setProperty("categoryChip", true);
        chipsLayout->addWidget(categoryChip);
    }

    chipsLayout->addStretch();
    infoLayout->addWidget(chipsWidget);
    mainLayout->addLayout(infoLayout, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());

    auto createBtn = [&](const QString &iconPath, const QString &tip) -> QPushButton* {
        QPushButton *btn = new QPushButton();
        btn->setObjectName("modCardActionBtn");
        btn->setProperty("cardRole", "actionBtn");
        btn->setFixedSize(32, 32);
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
        btn->setIconSize(QSize(18, 18));
        btn->setProperty("actionType", tip);
        return btn;
    };

    QPushButton *playBtn = createBtn(":/Images/Icons/play.svg", tr("启动游戏"));
    connect(playBtn, &QPushButton::clicked, [this, instancePath = instanceInfo.absoluteFilePath()]() {
        onLaunchGameClicked(instancePath);
    });
    btnLayout->addWidget(playBtn);

    QPushButton *settingsBtn = createBtn(":/Images/Icons/instance_settings.svg", tr("实例设置"));
    connect(settingsBtn, &QPushButton::clicked, [this, instancePath = instanceInfo.absoluteFilePath()]() {
        emit instanceSelected(instancePath);
    });
    btnLayout->addWidget(settingsBtn);



    QPushButton *exportBtn = createBtn(":/Images/Icons/export.svg", tr("导出整合包"));
    connect(exportBtn, &QPushButton::clicked, [this, instancePath = instanceInfo.absoluteFilePath()]() {
        emit exportModpackRequested(instancePath);
    });
    btnLayout->addWidget(exportBtn);

    QPushButton *deleteBtn = createBtn(":/Images/Icons/delete.svg", tr("删除实例"));
    deleteBtn->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#F44336"), 18));
    connect(deleteBtn, &QPushButton::clicked, [this, instancePath = instanceInfo.absoluteFilePath()]() {
        onDeleteInstanceClicked(instancePath);
    });
    btnLayout->addWidget(deleteBtn);

    mainLayout->addLayout(btnLayout);

    listWidget->setItemWidget(item, cardWidget);
    item->setSizeHint(QSize(0, 76));
}

void InstanceSelectPage::onDeleteInstanceClicked(const QString &instancePath)
{
    AppMessageBox::StandardButton reply = AppMessageBox::question(this, tr("删除实例"),
        tr("确定要删除此实例吗？\n%1").arg(instancePath),
        AppMessageBox::Yes | AppMessageBox::No);
    if (reply == AppMessageBox::Yes) {
        QDir dir(instancePath);
        if (dir.removeRecursively()) {
            SettingsManager::instance()->setInstanceCategory(instancePath, QString());
            loadInstances();
            updateInstanceList();
        } else {
            NotificationManager::showError(this, tr("删除实例失败！"));
        }
    }
}

void InstanceSelectPage::onInstanceItemClicked(QListWidgetItem *item)
{
    QString instancePath = item->data(Qt::UserRole).toString();
    emit instanceSelected(instancePath);
    emit backToMainRequested();
}

void InstanceSelectPage::onInstanceContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_instanceListWidget->itemAt(pos);
    if (!item)
    {
        return;
    }

    QString instancePath = item->data(Qt::UserRole).toString();
    if (instancePath.isEmpty())
    {
        return;
    }

    // 高亮当前右键项
    m_instanceListWidget->setCurrentItem(item);

    QMenu menu(this);
    menu.setStyleSheet(m_instanceListWidget->styleSheet());

    QAction *launchAction = menu.addAction(tr("启动游戏"));
    QAction *settingsAction = menu.addAction(tr("实例设置"));
    menu.addSeparator();
    QMenu *categoryMenu = menu.addMenu(tr("移动分类"));
    buildCategoryMenu(categoryMenu, instancePath);
    menu.addSeparator();
    QAction *exportAction = menu.addAction(tr("导出整合包"));
    menu.addSeparator();
    QAction *openFolderAction = menu.addAction(tr("打开实例文件夹"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("删除实例"));
    deleteAction->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#F44336"), 16));

    QAction *chosen = menu.exec(m_instanceListWidget->mapToGlobal(pos));

    if (chosen == launchAction)
    {
        onLaunchGameClicked(instancePath);
    }
    else if (chosen == settingsAction)
    {
        emit instanceSelected(instancePath);
        emit backToMainRequested();
    }
    else if (chosen == exportAction)
    {
        emit exportModpackRequested(instancePath);
    }
    else if (chosen == openFolderAction)
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(instancePath));
    }
    else if (chosen == deleteAction)
    {
        onDeleteInstanceClicked(instancePath);
    }
}

// 实例文件夹路径的比较键：统一为绝对路径（正斜杠、无冗余段），
// Windows 上文件系统不区分大小写，故再忽略大小写，
// 避免 D:/A/.minecraft 与 d:\A\.minecraft 被当成两个文件夹重复绑定
static QString instanceFolderKey(const QString &path)
{
    const QString key = QDir(path).absolutePath();
#ifdef Q_OS_WIN
    return key.toLower();
#else
    return key;
#endif
}

void InstanceSelectPage::onBindFolderClicked()
{
    QString folderPath = AppFileDialog::getExistingDirectory(this, tr("选择 Minecraft 文件夹"), QCoreApplication::applicationDirPath());
    if (folderPath.isEmpty())
        return;

    // 统一路径格式，避免与设置中已存路径因分隔符/结尾差异导致重复
    QString folderKey = QDir(folderPath).absolutePath();

    // 兼容 PCL2/HMCL 用户习惯：选中 .minecraft 的上层目录时，
    // 自动下钻到其中的 .minecraft / minecraft 文件夹再绑定
    if (!QDir(folderKey).exists("versions")) {
        const QStringList innerCandidates = { QStringLiteral(".minecraft"), QStringLiteral("minecraft") };
        for (const QString &sub : innerCandidates) {
            const QString innerPath = QDir(folderKey).filePath(sub);
            if (QDir(innerPath).exists("versions")) {
                folderKey = QDir(innerPath).absolutePath();
                break;
            }
        }
    }

    // Check if folder is valid (contains versions directory)
    QDir folderDir(folderKey);
    if (!folderDir.exists("versions")) {
        NotificationManager::showError(this, tr("所选文件夹不是有效的 Minecraft 文件夹，缺少 versions 目录！"));
        return;
    }

    // 已绑定过则直接选中已有按钮，避免重复添加
    const QString newKey = instanceFolderKey(folderKey);
    for (QPushButton *btn : m_instanceFolderButtons) {
        if (instanceFolderKey(btn->property("folderPath").toString()) == newKey) {
            setSelectedFolder(btn);
            NotificationManager::showInfo(this, tr("该文件夹已绑定"));
            return;
        }
    }

    const QString folderName = folderDisplayName(folderKey);

    // Save to SettingsManager（内部会做路径去重）
    SettingsManager::instance()->addInstanceFolder(folderName, folderKey, false);

    // 添加侧边栏按钮并选中
    QPushButton *newBtn = createFolderNavButton(folderName, folderKey, false);
    m_instanceFolderButtons.append(newBtn);
    m_folderButtonsLayout->addWidget(newBtn);
    setSelectedFolder(newBtn);
}

void InstanceSelectPage::onInstallNewInstanceClicked()
{
    emit installNewInstanceRequested();
}

void InstanceSelectPage::onDownloadModpackClicked()
{
    emit downloadModpackRequested();
}

void InstanceSelectPage::onImportModpackClicked()
{
    emit importModpackRequested();
}

void InstanceSelectPage::initLauncher()
{
    // Initialize game launcher
    m_gameLauncher = GameLauncher::instance();
    
    // Connect launcher signals
    connect(m_gameLauncher, &GameLauncher::launchStatusChanged, this, [this](GameLauncher::LaunchStatus status) {
        onLaunchStatusChanged(static_cast<int>(status));
    });
    connect(m_gameLauncher, &GameLauncher::gameStarted, this, &InstanceSelectPage::onGameStarted);
    connect(m_gameLauncher, &GameLauncher::gameStopped, this, &InstanceSelectPage::onGameStopped);
    connect(m_gameLauncher, &GameLauncher::gameCrashed, this, &InstanceSelectPage::onGameCrashed);
    connect(m_gameLauncher, &GameLauncher::launchProgressChanged, this, &InstanceSelectPage::onLaunchProgressChanged);
}

void InstanceSelectPage::onLaunchStatusChanged(int status)
{
    // Update UI based on launch status
    switch (status) {
    case 0: // Idle
        break;
    case 1: // Launching
        break;
    case 2: // Running
        break;
    case 3: // Failed
        NotificationManager::showError(this, m_gameLauncher->errorMessage());
        break;
    case 4: // Stopped
        break;
    }
}

void InstanceSelectPage::onGameStarted()
{
    NotificationManager::showSuccess(this, tr("游戏已成功启动！"));
}

void InstanceSelectPage::onGameStopped(int exitCode)
{
    if (exitCode == 0) {
        NotificationManager::showInfo(this, tr("游戏已正常退出。"));
    } else {
        NotificationManager::showError(this, tr("游戏以退出代码 %1 结束。").arg(exitCode));
    }
}

void InstanceSelectPage::onGameCrashed(const QString& error)
{
    NotificationManager::showError(this, tr("游戏启动失败：%1").arg(error));
}

void InstanceSelectPage::onLaunchProgressChanged(int progress, const QString& message)
{
    // Show progress if needed
    qDebug() << "[InstanceSelectPage]" << "Launch progress:" << progress << "% -" << message;
}

// 后台线程中计算所有实例的文件夹大小
QMap<QString, qint64> InstanceSelectPage::calculateSizesStatic(const QStringList &instancePaths)
{
    QMap<QString, qint64> result;
    for (const QString &path : instancePaths) {
        qint64 totalSize = 0;
        QDirIterator sizeIt(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (sizeIt.hasNext()) {
            sizeIt.next();
            totalSize += sizeIt.fileInfo().size();
        }
        result[path] = totalSize;
    }
    return result;
}

void InstanceSelectPage::startAsyncSizeCalculation()
{
    // 收集所有 sizeBytes == -1（尚未计算）的实例
    QStringList pathsToCalculate;
    for (const QFileInfo &fi : m_instances) {
        InstanceInfo *cached = m_instanceInfoCache[fi.absoluteFilePath()];
        if (cached && cached->sizeBytes >= 0)
            continue;
        pathsToCalculate.append(fi.absoluteFilePath());
    }
    if (pathsToCalculate.isEmpty())
        return;

    m_sizeWatcher.setFuture(QtConcurrent::run(calculateSizesStatic, pathsToCalculate));
}

void InstanceSelectPage::onAsyncSizesCalculated()
{
    QMap<QString, qint64> sizes = m_sizeWatcher.future().result();
    for (auto it = sizes.constBegin(); it != sizes.constEnd(); ++it) {
        // 更新缓存中的 sizeBytes
        InstanceInfo *cached = m_instanceInfoCache[it.key()];
        if (cached) {
            cached->sizeBytes = it.value();
        }
        // 更新 UI 中的大小标签
        QLabel *label = m_instanceSizeLabels.value(it.key());
        if (label) {
            qint64 bytes = it.value();
            QString sizeStr;
            if (bytes >= 1024 * 1024)
                sizeStr = QString("%1 MB").arg(bytes / (1024 * 1024));
            else if (bytes >= 1024)
                sizeStr = QString("%1 KB").arg(bytes / 1024);
            else
                sizeStr = QString("%1 B").arg(bytes);
            label->setText(sizeStr);
        }
    }
}

void InstanceSelectPage::onLaunchGameClicked(const QString& instancePath)
{
    // 交由 MainWindow 统一处理：显示任务卡片 + 启动游戏
    // 避免页面内直接调用 launchGame 导致与 MainWindow 的启动逻辑重复
    emit launchGameRequested(instancePath);
}

void InstanceSelectPage::setSelectedFolder(QPushButton *target)
{
    if (!target) return;

    // 清除所有按钮的选中状态与图标颜色
    for (QPushButton *btn : m_instanceFolderButtons) {
        btn->setChecked(false);
        QString iconPath = btn->property("iconPath").toString();
        if (!iconPath.isEmpty()) {
            btn->setIcon(loadColoredIcon(iconPath, m_iconNormalColor));
        }
    }

    // 设置目标按钮选中 + 切换图标为选中色
    target->setChecked(true);
    QString iconPath = target->property("iconPath").toString();
    if (!iconPath.isEmpty()) {
        target->setIcon(loadColoredIcon(iconPath, m_iconSelectedColor));
    }

    // 滑动高亮到目标按钮
    slideHighlightTo(target);

    // 更新当前文件夹路径并重新加载实例列表
    m_currentFolderPath = target->property("folderPath").toString();
    loadInstances();
    updateInstanceList();
}

void InstanceSelectPage::slideHighlightTo(QPushButton *target)
{
    if (!target || !m_folderHighlight) return;

    // 按钮是 m_folderButtonsWidget 的子控件，geometry 是相对其父级的坐标；
    // 高亮条挂在 m_leftWidget 下，需换算到 m_leftWidget 坐标系，否则高亮会跑偏
    QRect endRect(target->mapTo(m_leftWidget, QPoint(0, 0)), target->size());
    // 按钮刚创建时 geometry 可能尚未就绪，延迟到下一次事件循环重试，
    // 确保高亮定位准确（调用方无需关心时序）
    if (!endRect.isValid() || endRect.width() <= 0) {
        QPushButton *btnPtr = target;
        QTimer::singleShot(0, this, [this, btnPtr]() { slideHighlightTo(btnPtr); });
        return;
    }

    m_folderHighlight->setVisible(true);

    auto *anim = new QPropertyAnimation(m_folderHighlight, "geometry");
    anim->setDuration(250);
    anim->setStartValue(m_folderHighlight->geometry());
    anim->setEndValue(endRect);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void InstanceSelectPage::animateFolderEntrance()
{
    if (m_instanceFolderButtons.isEmpty()) return;

    auto *group = new QSequentialAnimationGroup(this);

    for (QPushButton *btn : m_instanceFolderButtons) {
        auto *effect = new QGraphicsOpacityEffect(btn);
        effect->setOpacity(0.0);
        btn->setGraphicsEffect(effect);

        auto *anim = new QPropertyAnimation(effect, "opacity");
        anim->setDuration(120);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);

        group->addPause(25);
        group->addAnimation(anim);
    }

    connect(group, &QSequentialAnimationGroup::finished, this, [this]() {
        for (QPushButton *btn : m_instanceFolderButtons) {
            if (btn) {
                btn->setGraphicsEffect(nullptr);
            }
        }
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

QIcon InstanceSelectPage::loadColoredIcon(const QString &path, const QColor &color) const
{
    return IconHelper::loadColoredIcon(path, color, 16);
}

/* ============ 实例分类 ============ */

void InstanceSelectPage::initCategorySection()
{
    if (!m_allCategoryBtn) {
        m_allCategoryBtn = new QPushButton(tr("全部"));
        m_allCategoryBtn->setObjectName("instanceFolderNavBtn");
        m_allCategoryBtn->setFixedHeight(34);
        m_allCategoryBtn->setCheckable(true);
        m_allCategoryBtn->setChecked(true);
        m_allCategoryBtn->setCursor(Qt::PointingHandCursor);
        m_allCategoryBtn->setIconSize(QSize(16, 16));
        m_allCategoryBtn->setIcon(loadColoredIcon(":/Images/Icons/nav_general.svg", m_iconSelectedColor));
        m_categoryButtonsLayout->addWidget(m_allCategoryBtn);
        connect(m_allCategoryBtn, &QPushButton::clicked, this, [this]() {
            setSelectedCategory(m_allCategoryBtn);
        });
    }
    rebuildCategoryButtons();
}

void InstanceSelectPage::rebuildCategoryButtons()
{
    for (QPushButton *btn : m_categoryButtons) {
        m_categoryButtonsLayout->removeWidget(btn);
        delete btn;
    }
    m_categoryButtons.clear();

    // 全部按钮状态与选中色同步
    if (m_allCategoryBtn) {
        const bool active = m_currentCategory.isEmpty();
        m_allCategoryBtn->setChecked(active);
        m_allCategoryBtn->setIcon(loadColoredIcon(":/Images/Icons/nav_general.svg",
                                                  active ? m_iconSelectedColor : m_iconNormalColor));
    }

    const QStringList categories = SettingsManager::instance()->getInstanceCategories();
    for (const QString &cat : categories) {
        QPushButton *btn = new QPushButton(cat);
        btn->setObjectName("instanceFolderNavBtn");
        btn->setFixedHeight(34);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIconSize(QSize(16, 16));
        btn->setProperty("categoryName", cat);
        btn->setProperty("iconPath", ":/Images/Icons/nav_folder.svg");
        const bool active = (cat == m_currentCategory);
        btn->setIcon(loadColoredIcon(":/Images/Icons/nav_folder.svg",
                                     active ? m_iconSelectedColor : m_iconNormalColor));
        btn->setChecked(active);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            setSelectedCategory(btn);
        });
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, btn](const QPoint &pos) {
            QMenu menu(this);
            menu.setStyleSheet(this->styleSheet());
            QAction *renameAction = menu.addAction(tr("重命名分类"));
            QAction *deleteAction = menu.addAction(tr("删除分类"));
            deleteAction->setIcon(IconHelper::loadColoredIcon(":/Images/Icons/delete.svg", QColor("#F44336"), 16));
            QAction *chosen = menu.exec(btn->mapToGlobal(pos));
            if (chosen == renameAction) {
                onRenameCategory(btn->property("categoryName").toString());
            } else if (chosen == deleteAction) {
                onDeleteCategory(btn->property("categoryName").toString());
            }
        });

        m_categoryButtonsLayout->addWidget(btn);
        m_categoryButtons.append(btn);
    }

    // 滑动高亮定位到当前选中的分类
    QTimer::singleShot(0, this, [this]() {
        if (!m_currentCategory.isEmpty()) {
            for (QPushButton *btn : m_categoryButtons) {
                if (btn->property("categoryName").toString() == m_currentCategory) {
                    slideCategoryHighlightTo(btn);
                    break;
                }
            }
        } else if (m_allCategoryBtn) {
            slideCategoryHighlightTo(m_allCategoryBtn);
        }
    });
}

void InstanceSelectPage::setSelectedCategory(QPushButton *target)
{
    if (!target) return;

    for (QPushButton *btn : m_categoryButtons) {
        btn->setChecked(false);
        QString iconPath = btn->property("iconPath").toString();
        if (!iconPath.isEmpty()) {
            btn->setIcon(loadColoredIcon(iconPath, m_iconNormalColor));
        }
    }
    if (m_allCategoryBtn && m_allCategoryBtn != target) {
        m_allCategoryBtn->setChecked(false);
        m_allCategoryBtn->setIcon(loadColoredIcon(":/Images/Icons/nav_general.svg", m_iconNormalColor));
    }

    target->setChecked(true);
    m_currentCategory = target->property("categoryName").toString();

    QString iconPath = target->property("iconPath").toString();
    if (iconPath.isEmpty()) {
        iconPath = ":/Images/Icons/nav_general.svg";
    }
    target->setIcon(loadColoredIcon(iconPath, m_iconSelectedColor));

    slideCategoryHighlightTo(target);
    updateInstanceList();
}

void InstanceSelectPage::slideCategoryHighlightTo(QPushButton *target)
{
    if (!target || !m_categoryHighlight) return;

    // 分类按钮是 m_categoryButtonsWidget 的子控件，需换算到 m_leftWidget 坐标系
    QRect endRect(target->mapTo(m_leftWidget, QPoint(0, 0)), target->size());
    if (!endRect.isValid() || endRect.width() <= 0) {
        QPushButton *btnPtr = target;
        QTimer::singleShot(0, this, [this, btnPtr]() { slideCategoryHighlightTo(btnPtr); });
        return;
    }

    m_categoryHighlight->setVisible(true);

    auto *anim = new QPropertyAnimation(m_categoryHighlight, "geometry");
    anim->setDuration(250);
    anim->setStartValue(m_categoryHighlight->geometry());
    anim->setEndValue(endRect);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

bool InstanceSelectPage::matchesCategory(const QString &instancePath) const
{
    if (m_currentCategory.isEmpty()) {
        return true;
    }
    return SettingsManager::instance()->getInstanceCategory(instancePath) == m_currentCategory;
}

void InstanceSelectPage::buildCategoryMenu(QMenu *menu, const QString &instancePath)
{
    SettingsManager *sm = SettingsManager::instance();
    const QString currentCat = sm->getInstanceCategory(instancePath);
    const QStringList categories = sm->getInstanceCategories();

    QAction *noneAction = menu->addAction(tr("未分类"));
    noneAction->setCheckable(true);
    noneAction->setChecked(currentCat.isEmpty());
    for (const QString &cat : categories) {
        QAction *act = menu->addAction(cat);
        act->setCheckable(true);
        act->setChecked(currentCat == cat);
    }
    menu->addSeparator();
    menu->addAction(tr("新建分类..."));

    const QList<QAction *> actions = menu->actions();
    QAction *firstAction = actions.value(0);
    QAction *lastAction = actions.value(actions.size() - 1);

    connect(menu, &QMenu::triggered, this, [this, firstAction, lastAction, instancePath](QAction *action) {
        if (action == firstAction) {
            // 移入未分类
            SettingsManager::instance()->setInstanceCategory(instancePath, QString());
        } else if (action == lastAction) {
            // 新建分类并移入
            bool ok = false;
            QString name = AppInputDialog::getItem(this, tr("新建分类"), tr("分类名称："),
                                                   categoryNamePresets(), 0, true, &ok).trimmed();
            if (ok && !name.isEmpty()) {
                SettingsManager *sm2 = SettingsManager::instance();
                if (sm2->getInstanceCategories().contains(name)) {
                    NotificationManager::showError(this, tr("分类「%1」已存在").arg(name));
                } else {
                    sm2->addInstanceCategory(name);
                    sm2->setInstanceCategory(instancePath, name);
                    rebuildCategoryButtons();
                }
            }
        } else {
            SettingsManager::instance()->setInstanceCategory(instancePath, action->text());
        }
        updateInstanceList();
    });
}

void InstanceSelectPage::onAddCategoryClicked()
{
    bool ok = false;
    QString name = AppInputDialog::getItem(this, tr("新建分类"), tr("分类名称："),
                                           categoryNamePresets(), 0, true, &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    SettingsManager *sm = SettingsManager::instance();
    if (sm->getInstanceCategories().contains(name)) {
        NotificationManager::showError(this, tr("分类「%1」已存在").arg(name));
        return;
    }
    sm->addInstanceCategory(name);
    rebuildCategoryButtons();
}

void InstanceSelectPage::onRenameCategory(const QString &category)
{
    if (category.isEmpty()) return;

    bool ok = false;
    QString name = AppInputDialog::getText(this, tr("重命名分类"), tr("新分类名称："),
                                           QLineEdit::Normal, category, &ok).trimmed();
    if (!ok || name.isEmpty() || name == category) {
        return;
    }
    SettingsManager *sm = SettingsManager::instance();
    if (sm->getInstanceCategories().contains(name)) {
        NotificationManager::showError(this, tr("分类「%1」已存在").arg(name));
        return;
    }
    sm->renameInstanceCategory(category, name);
    if (m_currentCategory == category) {
        m_currentCategory = name;
    }
    rebuildCategoryButtons();
    updateInstanceList();
}

void InstanceSelectPage::onDeleteCategory(const QString &category)
{
    if (category.isEmpty()) return;

    AppMessageBox::StandardButton reply = AppMessageBox::question(this, tr("删除分类"),
        tr("确定删除分类「%1」吗？\n该分类下的实例将变为未分类。").arg(category),
        AppMessageBox::Yes | AppMessageBox::No);
    if (reply != AppMessageBox::Yes) {
        return;
    }
    SettingsManager::instance()->removeInstanceCategory(category);
    if (m_currentCategory == category) {
        m_currentCategory.clear();
    }
    rebuildCategoryButtons();
    updateInstanceList();
}
