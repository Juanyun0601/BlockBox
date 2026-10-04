/**
 * @file   InstanceOverviewPage.cpp
 * @brief  实例管理 - 概览页实现
 * @author BlockBox Team
 * @date   2026-06-27
 */
#include "InstanceOverviewPage.h"

#include "components/OutlinedLabel.h"
#include "utils/IconHelper.h"
#include "utils/SettingsManager.h"
#include "utils/ThemeManager.h"
#include "utils/UiMetrics.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPixmap>
#include <QShowEvent>
#include <QScrollArea>
#include <QFrame>
#include <QtConcurrent>

InstanceOverviewPage::InstanceOverviewPage(QWidget *parent)
    : QWidget(parent)
    , m_headerCard(nullptr)
    , m_instanceIconLabel(nullptr)
    , m_instanceNameLabel(nullptr)
    , m_versionLabel(nullptr)
    , m_loaderLabel(nullptr)
    , m_lastPlayedLabel(nullptr)
    , m_launchBtn(nullptr)
    , m_statsCardsRow(nullptr)
    , m_modCard(nullptr)
    , m_modCountLabel(nullptr)
    , m_resourcePackCard(nullptr)
    , m_resourcePackCountLabel(nullptr)
    , m_shaderCard(nullptr)
    , m_shaderCountLabel(nullptr)
    , m_savesCard(nullptr)
    , m_savesCountLabel(nullptr)
    , m_sizeCard(nullptr)
    , m_sizeLabel(nullptr)
    , m_launchCountCard(nullptr)
    , m_launchCountLabel(nullptr)
    , m_actionsSection(nullptr)
    , m_openFolderBtn(nullptr)
    , m_settingsBtn(nullptr)
    , m_modsBtn(nullptr)
    , m_exportBtn(nullptr)
    , m_resourcePacksBtn(nullptr)
    , m_shadersBtn(nullptr)
    , m_savesBtn(nullptr)
    , m_screenshotsBtn(nullptr)
    , m_pathCard(nullptr)
    , m_pathLabel(nullptr)
{
    initUI();
}

InstanceOverviewPage::~InstanceOverviewPage()
{
}

void InstanceOverviewPage::initUI()
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // 滚动区域
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setObjectName("overviewScrollArea");

    QWidget *scrollContent = new QWidget(scrollArea);
    scrollContent->setObjectName("overviewScrollContent");

    QVBoxLayout *mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(24, 20, 24, 24);
    mainLayout->setSpacing(16);

    // ========== 1. 头部卡片 ==========
    m_headerCard = new QWidget(scrollContent);
    m_headerCard->setObjectName("overviewHeaderCard");
    QHBoxLayout *headerLayout = new QHBoxLayout(m_headerCard);
    headerLayout->setContentsMargins(20, 16, 20, 16);
    headerLayout->setSpacing(16);

    // 左侧：图标 + 信息
    QWidget *headerLeft = new QWidget(m_headerCard);
    QHBoxLayout *headerLeftLayout = new QHBoxLayout(headerLeft);
    headerLeftLayout->setContentsMargins(0, 0, 0, 0);
    headerLeftLayout->setSpacing(14);

    // 实例图标
    m_instanceIconLabel = new QLabel(headerLeft);
    m_instanceIconLabel->setObjectName("overviewIcon");
    m_instanceIconLabel->setFixedSize(56, 56);
    m_instanceIconLabel->setAlignment(Qt::AlignCenter);
    headerLeftLayout->addWidget(m_instanceIconLabel);

    // 信息区域
    QWidget *headerInfo = new QWidget(headerLeft);
    QVBoxLayout *headerInfoLayout = new QVBoxLayout(headerInfo);
    headerInfoLayout->setContentsMargins(0, 0, 0, 0);
    headerInfoLayout->setSpacing(4);

    m_instanceNameLabel = new OutlinedLabel(tr("未选择实例"), headerInfo);
    m_instanceNameLabel->setObjectName("overviewInstanceName");
    headerInfoLayout->addWidget(m_instanceNameLabel);

    QWidget *tagRow = new QWidget(headerInfo);
    QHBoxLayout *tagRowLayout = new QHBoxLayout(tagRow);
    tagRowLayout->setContentsMargins(0, 0, 0, 0);
    tagRowLayout->setSpacing(8);

    m_versionLabel = new QLabel(tagRow);
    m_versionLabel->setObjectName("overviewTag");
    tagRowLayout->addWidget(m_versionLabel);

    m_loaderLabel = new QLabel(tagRow);
    m_loaderLabel->setObjectName("overviewTag");
    tagRowLayout->addWidget(m_loaderLabel);

    m_lastPlayedLabel = new QLabel(tagRow);
    m_lastPlayedLabel->setObjectName("overviewSubText");
    tagRowLayout->addWidget(m_lastPlayedLabel);

    tagRowLayout->addStretch();
    headerInfoLayout->addWidget(tagRow);
    headerLeftLayout->addWidget(headerInfo);

    headerLayout->addWidget(headerLeft);
    headerLayout->addStretch();

    // 右侧：启动按钮
    m_launchBtn = new QPushButton(tr("启动游戏"), m_headerCard);
    m_launchBtn->setObjectName("overviewLaunchBtn");
    m_launchBtn->setFixedSize(130, 44);
    m_launchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_launchBtn, &QPushButton::clicked, this, &InstanceOverviewPage::launchGameRequested);
    headerLayout->addWidget(m_launchBtn);

    mainLayout->addWidget(m_headerCard);

    // ========== 2. 统计卡片行 ==========
    m_statsCardsRow = new QWidget(scrollContent);
    QHBoxLayout *statsLayout = new QHBoxLayout(m_statsCardsRow);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(12);

    QColor themeColor(ThemeManager::instance()->currentThemeColor());

    auto createStatCard = [&](const QString &iconPath, const QString &title, QLabel *&countLabel, QWidget *&card) {
        card = new QWidget(m_statsCardsRow);
        card->setObjectName("overviewStatCard");
        card->setCursor(Qt::PointingHandCursor);
        // 固定宽度：各统计卡片边框对齐，宽窄一致
        card->setFixedWidth(UiMetrics::kStatCardWidth);
        QVBoxLayout *layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(6);

        QLabel *iconLabel = new QLabel(card);
        iconLabel->setObjectName("overviewStatIcon");
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setFixedSize(32, 32);
        QIcon statIcon = IconHelper::loadColoredIcon(iconPath, themeColor, 18);
        iconLabel->setPixmap(statIcon.pixmap(18, 18));
        layout->addWidget(iconLabel, 0, Qt::AlignHCenter);

        countLabel = new QLabel("--", card);
        countLabel->setObjectName("overviewStatCount");
        countLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(countLabel, 0, Qt::AlignHCenter);

        QLabel *titleLabel = new QLabel(title, card);
        titleLabel->setObjectName("overviewStatTitle");
        titleLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(titleLabel, 0, Qt::AlignHCenter);
    };

    createStatCard(":/Images/Icons/nav_mods.svg", tr("模组"), m_modCountLabel, m_modCard);
    createStatCard(":/Images/Icons/nav_resourcepacks.svg", tr("资源包"), m_resourcePackCountLabel, m_resourcePackCard);
    createStatCard(":/Images/Icons/nav_shaders.svg", tr("光影包"), m_shaderCountLabel, m_shaderCard);
    createStatCard(":/Images/Icons/nav_worlds.svg", tr("存档"), m_savesCountLabel, m_savesCard);
    createStatCard(":/Images/Icons/size.svg", tr("大小"), m_sizeLabel, m_sizeCard);
    createStatCard(":/Images/Icons/play.svg", tr("启动次数"), m_launchCountLabel, m_launchCountCard);

    statsLayout->addWidget(m_modCard);
    statsLayout->addWidget(m_resourcePackCard);
    statsLayout->addWidget(m_shaderCard);
    statsLayout->addWidget(m_savesCard);
    statsLayout->addWidget(m_sizeCard);
    statsLayout->addWidget(m_launchCountCard);
    statsLayout->addStretch();

    mainLayout->addWidget(m_statsCardsRow);

    // ========== 3. 快捷操作区 ==========
    m_actionsSection = new QWidget(scrollContent);
    m_actionsSection->setObjectName("overviewActionsSection");
    QVBoxLayout *actionsLayout = new QVBoxLayout(m_actionsSection);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);

    QLabel *actionsTitle = new QLabel(tr("快捷操作"), m_actionsSection);
    actionsTitle->setObjectName("overviewSectionTitle");
    actionsLayout->addWidget(actionsTitle);

    QWidget *actionsGrid = new QWidget(m_actionsSection);
    QGridLayout *gridLayout = new QGridLayout(actionsGrid);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(10);

    auto createActionBtn = [&](const QString &text, const QString &iconPath, const char *signal) -> QPushButton* {
        QPushButton *btn = new QPushButton(text, actionsGrid);
        btn->setObjectName("overviewActionBtn");
        btn->setFixedHeight(42);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(IconHelper::loadColoredIcon(iconPath, themeColor, 18));
        btn->setIconSize(QSize(18, 18));
        connect(btn, SIGNAL(clicked()), this, signal);
        return btn;
    };

    m_openFolderBtn = createActionBtn(tr("打开文件夹"), ":/Images/Icons/folder.svg", SIGNAL(openFolderRequested()));
    m_settingsBtn = createActionBtn(tr("实例设置"), ":/Images/Icons/instance_settings.svg", SIGNAL(settingsRequested()));
    m_modsBtn = createActionBtn(tr("模组管理"), ":/Images/Icons/nav_mods.svg", SIGNAL(modsRequested()));
    m_exportBtn = createActionBtn(tr("导出整合包"), ":/Images/Icons/export.svg", SIGNAL(exportRequested()));
    m_resourcePacksBtn = createActionBtn(tr("资源包"), ":/Images/Icons/nav_resourcepacks.svg", SIGNAL(resourcePacksRequested()));
    m_shadersBtn = createActionBtn(tr("光影包"), ":/Images/Icons/nav_shaders.svg", SIGNAL(shadersRequested()));
    m_savesBtn = createActionBtn(tr("存档管理"), ":/Images/Icons/nav_worlds.svg", SIGNAL(savesRequested()));
    m_screenshotsBtn = createActionBtn(tr("截图管理"), ":/Images/Icons/screenshot.svg", SIGNAL(screenshotsRequested()));

    gridLayout->addWidget(m_openFolderBtn, 0, 0);
    gridLayout->addWidget(m_settingsBtn, 0, 1);
    gridLayout->addWidget(m_modsBtn, 0, 2);
    gridLayout->addWidget(m_exportBtn, 0, 3);
    gridLayout->addWidget(m_resourcePacksBtn, 1, 0);
    gridLayout->addWidget(m_shadersBtn, 1, 1);
    gridLayout->addWidget(m_savesBtn, 1, 2);
    gridLayout->addWidget(m_screenshotsBtn, 1, 3);

    actionsLayout->addWidget(actionsGrid);
    mainLayout->addWidget(m_actionsSection);

    // ========== 4. 实例路径 ==========
    m_pathCard = new QWidget(scrollContent);
    m_pathCard->setObjectName("overviewPathCard");
    QHBoxLayout *pathLayout = new QHBoxLayout(m_pathCard);
    pathLayout->setContentsMargins(16, 12, 16, 12);
    pathLayout->setSpacing(8);

    QLabel *pathIcon = new QLabel(m_pathCard);
    pathIcon->setObjectName("overviewPathIcon");
    pathIcon->setFixedSize(20, 20);
    QIcon pathIconImg = IconHelper::loadColoredIcon(":/Images/Icons/folder.svg", themeColor, 16);
    pathIcon->setPixmap(pathIconImg.pixmap(16, 16));
    pathLayout->addWidget(pathIcon);

    m_pathLabel = new QLabel(m_pathCard);
    m_pathLabel->setObjectName("overviewPathText");
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pathLabel->setWordWrap(true);
    pathLayout->addWidget(m_pathLabel, 1);

    mainLayout->addWidget(m_pathCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea);
}

void InstanceOverviewPage::setInstancePath(const QString &path)
{
    if (m_instancePath == path)
    {
        return;
    }
    m_instancePath = path;
    m_pathLabel->setText(path);

    // 读取最后游玩时间
    QFileInfo info(path);
    if (info.exists())
    {
        m_lastPlayed = info.lastModified().toString("yyyy-MM-dd hh:mm");
        m_lastPlayedLabel->setText(tr("最后修改: %1").arg(m_lastPlayed));
    }

    refreshStats();
}

void InstanceOverviewPage::setInstanceName(const QString &name)
{
    m_instanceName = name;
    m_instanceNameLabel->setText(name);
    if (m_iconPath.isEmpty())
        m_instanceIconLabel->setText(name.isEmpty() ? "" : name.left(1).toUpper());
    else
        updateIconPixmap();
}

void InstanceOverviewPage::setInstanceIcon(const QString &iconPath)
{
    m_iconPath = iconPath;
    updateIconPixmap();
}

void InstanceOverviewPage::updateIconPixmap()
{
    if (!m_instanceIconLabel)
        return;
    if (!m_iconPath.isEmpty() && QFile::exists(m_iconPath))
    {
        QPixmap pm(m_iconPath);
        if (!pm.isNull())
        {
            m_instanceIconLabel->setPixmap(
                pm.scaled(m_instanceIconLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_instanceIconLabel->setText(QString());
            return;
        }
    }
    const QString letter = m_instanceName.isEmpty() ? QString() : m_instanceName.left(1).toUpper();
    m_instanceIconLabel->setPixmap(QPixmap());
    m_instanceIconLabel->setText(letter);
}

void InstanceOverviewPage::setGameVersion(const QString &version)
{
    m_gameVersion = version;
    m_versionLabel->setText(version);
    m_versionLabel->setVisible(!version.isEmpty());
}

void InstanceOverviewPage::setLoaderInfo(const QString &loader)
{
    m_loaderInfo = loader;
    m_loaderLabel->setText(loader);
    m_loaderLabel->setVisible(!loader.isEmpty());
}

void InstanceOverviewPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshStats();
}

void InstanceOverviewPage::refreshStats()
{
    refreshModCount();
    refreshResourcePackCount();
    refreshShaderCount();
    refreshSaveCount();
    refreshInstanceSize();
    refreshLaunchCount();
}

void InstanceOverviewPage::refreshModCount()
{
    if (m_instancePath.isEmpty())
    {
        m_modCountLabel->setText("--");
        return;
    }
    QString modsDir = m_instancePath + "/mods";
    QDir dir(modsDir);
    if (!dir.exists())
    {
        m_modCountLabel->setText("0");
        return;
    }
    QStringList filters;
    filters << "*.jar" << "*.disabled";
    dir.setNameFilters(filters);
    int count = static_cast<int>(dir.entryList(QDir::Files).size());
    m_modCountLabel->setText(QString::number(count));
}

void InstanceOverviewPage::refreshResourcePackCount()
{
    if (m_instancePath.isEmpty())
    {
        m_resourcePackCountLabel->setText("--");
        return;
    }
    QString dir = m_instancePath + "/resourcepacks";
    QDir rpDir(dir);
    if (!rpDir.exists())
    {
        m_resourcePackCountLabel->setText("0");
        return;
    }
    rpDir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    int count = static_cast<int>(rpDir.entryList().size());
    m_resourcePackCountLabel->setText(QString::number(count));
}

void InstanceOverviewPage::refreshShaderCount()
{
    if (m_instancePath.isEmpty())
    {
        m_shaderCountLabel->setText("--");
        return;
    }
    QString dir = m_instancePath + "/shaderpacks";
    QDir sDir(dir);
    if (!sDir.exists())
    {
        m_shaderCountLabel->setText("0");
        return;
    }
    sDir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    int count = static_cast<int>(sDir.entryList().size());
    m_shaderCountLabel->setText(QString::number(count));
}

void InstanceOverviewPage::refreshSaveCount()
{
    if (m_instancePath.isEmpty())
    {
        m_savesCountLabel->setText("--");
        return;
    }
    QString dir = m_instancePath + "/saves";
    QDir savesDir(dir);
    if (!savesDir.exists())
    {
        m_savesCountLabel->setText("0");
        return;
    }
    savesDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    int count = static_cast<int>(savesDir.entryList().size());
    m_savesCountLabel->setText(QString::number(count));
}

void InstanceOverviewPage::refreshInstanceSize()
{
    if (m_instancePath.isEmpty())
    {
        m_sizeLabel->setText("--");
        return;
    }
    m_sizeLabel->setText(tr("计算中..."));

    // 递归遍历整个实例目录可能很慢，放到工作线程执行
    QString path = m_instancePath;
    (void)QtConcurrent::run([this, path]()
    {
        qint64 totalSize = 0;
        QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            it.next();
            totalSize += it.fileInfo().size();
        }
        // 回到主线程更新 UI
        QMetaObject::invokeMethod(this, [this, totalSize]()
        {
            m_sizeLabel->setText(formatFileSize(totalSize));
        }, Qt::QueuedConnection);
    });
}

void InstanceOverviewPage::refreshLaunchCount()
{
    if (m_instancePath.isEmpty())
    {
        m_launchCountLabel->setText("--");
        return;
    }
    const int count = SettingsManager::instance()
        ->getProperty("instance/" + m_instancePath + "/launchCount", 0).toInt();
    m_launchCountLabel->setText(QString::number(count));
}

QString InstanceOverviewPage::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
    {
        return QString::number(bytes) + " B";
    }
    else if (bytes < 1024 * 1024)
    {
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }
    else if (bytes < 1024LL * 1024 * 1024)
    {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    }
    else
    {
        return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
    }
}