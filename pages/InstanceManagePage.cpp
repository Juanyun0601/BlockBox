#include "InstanceManagePage.h"

#include <QLabel>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>

#include "components/BlurLoadingOverlay.h"
#include "pages/settings/InstanceGameSettingsPage.h"
#include "pages/InstanceModsPage.h"
#include "pages/InstanceFilePage.h"
#include "pages/InstanceOverviewPage.h"
#include "pages/LanTransferPage.h"
#include "pages/ModpackExportPage.h"
#include "pages/ServerManagePage.h"
#include "pages/ProjectionManagePage.h"
#include "pages/ProjectionEditPage.h"
#include "pages/SaveSettingsPage.h"
#include "pages/ShaderPackSettingsPage.h"
#include "pages/InstanceLogPage.h"
#include "utils/SettingsManager.h"

InstanceManagePage::InstanceManagePage(QWidget *parent)
    : QWidget(parent)
    , m_overviewPage(nullptr)
    , m_instanceSettingsPage(nullptr)
    , m_serverPage(nullptr)
    , m_projectionPage(nullptr)
    , m_exportPage(nullptr)
    , m_modsPage(nullptr)
    , m_savesPage(nullptr)
    , m_screenshotsPage(nullptr)
    , m_resourcePacksPage(nullptr)
    , m_shaderPacksPage(nullptr)
    , m_logPage(nullptr)
    , m_saveSettingsPage(nullptr)
    , m_shaderSettingsPage(nullptr)
    , m_projectionEditPage(nullptr)
{
    initUI();
}

InstanceManagePage::~InstanceManagePage()
{
}

void InstanceManagePage::setCurrentTab(int index)
{
    // Stack 索引: 0=概览, 1=设置, 2=导出, 3=模组, 4=存档,
    //            5=截图, 6=资源包, 7=光影包, 8=服务器管理, 9=蓝图,
    //            10=存档设置（非侧边栏页，仅由存档列表进入），
    //            11=光影设置（非侧边栏页，仅由光影包列表进入），
    //            12=投影编辑（非侧边栏页，仅由投影列表进入），
    //            13=日志, 14=文件传输
    // 侧边栏"修改"子项已改为直接前往安装新实例页进入修改模式，不再对应本页标签
    static const int tabMap[] = {0, 1, -1, 2, 14, 4, 8, 9, 5, 3, 6, 7, 13};
    if (index < 0 || index > 12)
        return;
    const int stackIndex = tabMap[index];
    if (stackIndex < 0)
        return;
    m_contentStack->setCurrentIndex(stackIndex);
    // 懒加载：进入对应标签页时才生成该页内容
    loadSubPage(stackIndex);
}

void InstanceManagePage::setCurrentInstancePath(const QString &instancePath)
{
    m_currentInstancePath = instancePath;

    // 存档设置页可能残留上个实例的信息，切换实例时重置
    if (m_saveSettingsPage)
        m_saveSettingsPage->reset();
    // 光影设置页可能残留上个实例的信息，切换实例时重置
    if (m_shaderSettingsPage)
        m_shaderSettingsPage->reset();
    // 投影编辑页可能残留上个实例的信息，切换实例时重置
    if (m_projectionEditPage)
        m_projectionEditPage->reset();

    // 立即加载可见的概览页，让用户尽快看到内容
    if (m_overviewPage)
        m_overviewPage->setInstancePath(instancePath);

    // 同步已保存的实例图标到概览页
    if (m_overviewPage && !instancePath.isEmpty())
    {
        QString iconPath = SettingsManager::instance()
            ->getProperty("instance/" + instancePath + "/iconPath").toString();
        m_overviewPage->setInstanceIcon(iconPath);
    }

    // 其余子页面（设置/导出/模组/存档/截图/资源包/光影包/服务器/蓝图/日志）
    // 改为在用户进入对应标签页时才加载，避免打开实例管理页时一次性读取所有目录

    // 若当前停留在某个子标签页（如模组页）时切换了实例，直接刷新该子页
    if (m_contentStack)
        loadSubPage(m_contentStack->currentIndex());
}

void InstanceManagePage::setInstanceName(const QString &name)
{
    if (m_overviewPage)
        m_overviewPage->setInstanceName(name);
    if (m_instanceSettingsPage)
        m_instanceSettingsPage->setInstanceName(name);
}

void InstanceManagePage::setInstanceIcon(const QString &iconPath)
{
    if (m_overviewPage)
        m_overviewPage->setInstanceIcon(iconPath);
    if (m_instanceSettingsPage)
        m_instanceSettingsPage->setInstanceIcon(iconPath);
}

void InstanceManagePage::setGameVersion(const QString &version)
{
    if (m_overviewPage)
        m_overviewPage->setGameVersion(version);
    if (m_instanceSettingsPage)
        m_instanceSettingsPage->setGameVersion(version);
}

void InstanceManagePage::setLoaderInfo(const QString &loader)
{
    if (m_overviewPage)
        m_overviewPage->setLoaderInfo(loader);
    if (m_instanceSettingsPage)
        m_instanceSettingsPage->setLoaderInfo(loader);
}

void InstanceManagePage::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_contentStack = new QStackedWidget(this);
    m_contentStack->setObjectName("instanceManageContentStack");

    // Stack 索引:
    // 0=概览, 1=设置, 2=导出整合包, 3=模组,
    // 4=存档, 5=截图, 6=资源包, 7=光影包, 8=服务器管理, 9=蓝图,
    // 10=存档设置（非侧边栏页，仅由存档列表进入），
    // 11=光影设置（非侧边栏页，仅由光影包列表进入）
    // 12=投影编辑（非侧边栏页，仅由投影列表进入）

    // 0: 概览页
    m_overviewPage = new InstanceOverviewPage(this);
    m_contentStack->addWidget(m_overviewPage); // index 0
    connect(m_overviewPage, &InstanceOverviewPage::launchGameRequested, this, [this]() {
        emit launchGameRequested();
        // 启动次数在 MainWindow 处理该信号时 +1，转发后立即刷新统计
        if (m_overviewPage)
            m_overviewPage->refreshStats();
    });
    connect(m_overviewPage, &InstanceOverviewPage::openFolderRequested, this, [this]() {
        if (!m_currentInstancePath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentInstancePath));
    });
    connect(m_overviewPage, &InstanceOverviewPage::modsRequested, this, [this]() {
        setCurrentTab(9); // 模组
    });
    connect(m_overviewPage, &InstanceOverviewPage::settingsRequested, this, [this]() {
        setCurrentTab(1); // 设置
    });
    connect(m_overviewPage, &InstanceOverviewPage::exportRequested, this, [this]() {
        setCurrentTab(3); // 导出整合包
    });

    // 1: 设置
    m_instanceSettingsPage = new InstanceGameSettingsPage(this);
    m_contentStack->addWidget(m_instanceSettingsPage); // index 1
    connect(m_instanceSettingsPage, &InstanceGameSettingsPage::instanceInfoChanged, this,
            [this](const QString &displayName, const QString &iconPath)
    {
        // 同步概览页名称与图标，并向上转发给主窗口更新顶栏
        if (displayName.isEmpty())
        {
            if (m_overviewPage)
                m_overviewPage->setInstanceName(m_currentInstancePath.split("/").last());
        }
        else if (m_overviewPage)
        {
            m_overviewPage->setInstanceName(displayName);
        }
        if (m_overviewPage)
            m_overviewPage->setInstanceIcon(iconPath);
        emit instanceInfoChanged(displayName, iconPath);
    });

    // 2: 导出整合包
    m_exportPage = new ModpackExportPage(this);
    m_contentStack->addWidget(m_exportPage); // index 2

    // 3: 模组
    m_modsPage = new InstanceModsPage(this);
    m_contentStack->addWidget(m_modsPage); // index 3
    connect(m_modsPage, &InstanceModsPage::modDetailRequested,
            this, &InstanceManagePage::modDetailRequested);
    connect(m_modsPage, &InstanceModsPage::modDownloadSearchRequested,
            this, &InstanceManagePage::modDownloadSearchRequested);

    // 4: 存档
    m_savesPage = new InstanceFilePage(InstanceFilePage::Save, this);
    m_contentStack->addWidget(m_savesPage); // index 4
    connect(m_savesPage, &InstanceFilePage::quickLaunchSaveRequested,
            this, &InstanceManagePage::quickLaunchSaveRequested);
    connect(m_savesPage, &InstanceFilePage::fileSearchRequested,
            this, [this](const QString &keyword) {
        emit fileSearchRequested(ContentType::World, keyword);
    });

    // 5: 截图
    m_screenshotsPage = new InstanceFilePage(InstanceFilePage::Screenshot, this);
    m_contentStack->addWidget(m_screenshotsPage); // index 5

    // 6: 资源包
    m_resourcePacksPage = new InstanceFilePage(InstanceFilePage::ResourcePack, this);
    m_contentStack->addWidget(m_resourcePacksPage); // index 6
    connect(m_resourcePacksPage, &InstanceFilePage::fileSearchRequested,
            this, [this](const QString &keyword) {
        emit fileSearchRequested(ContentType::ResourcePack, keyword);
    });

    // 7: 光影包
    m_shaderPacksPage = new InstanceFilePage(InstanceFilePage::ShaderPack, this);
    m_contentStack->addWidget(m_shaderPacksPage); // index 7
    connect(m_shaderPacksPage, &InstanceFilePage::fileSearchRequested,
            this, [this](const QString &keyword) {
        emit fileSearchRequested(ContentType::ShaderPack, keyword);
    });

    // 8: 服务器管理
    m_serverPage = new ServerManagePage(this);
    m_contentStack->addWidget(m_serverPage); // index 8
    connect(m_serverPage, &ServerManagePage::quickLaunchServerRequested,
            this, &InstanceManagePage::quickLaunchServerRequested);

    // 9: 蓝图管理
    m_projectionPage = new ProjectionManagePage(this);
    m_contentStack->addWidget(m_projectionPage); // index 9
    connect(m_projectionPage, &ProjectionManagePage::projectionEditRequested, this,
            [this](const QString &filePath) {
        if (m_projectionEditPage)
        {
            // 先切换到投影编辑页，再加载文件，避免在隐藏页上执行耗时解析
            m_contentStack->setCurrentWidget(m_projectionEditPage);
            m_projectionEditPage->setCurrentProjection(filePath);
            // 通知主窗口更新 TopBar 返回按钮与标题
            emit projectionEditOpened();
        }
    });

    // 10: 存档设置（由存档列表卡片按钮进入，非侧边栏页）
    m_saveSettingsPage = new SaveSettingsPage(this);
    m_contentStack->addWidget(m_saveSettingsPage); // index 10
    connect(m_saveSettingsPage, &SaveSettingsPage::saveRenamed, this,
            [this](const QString &oldName, const QString &newName) {
        Q_UNUSED(oldName);
        Q_UNUSED(newName);
        // 世界重命名后刷新存档列表
        if (m_savesPage)
            m_savesPage->refreshFileList();
    });
    connect(m_savesPage, &InstanceFilePage::saveSettingsRequested, this,
            [this](const QString &saveName) {
        if (m_currentInstancePath.isEmpty())
            return;
        if (m_saveSettingsPage)
        {
            m_saveSettingsPage->setCurrentSave(
                m_currentInstancePath + QStringLiteral("/saves/") + saveName, saveName);
            m_contentStack->setCurrentWidget(m_saveSettingsPage);
            // 通知主窗口更新 TopBar 返回按钮与标题
            emit saveSettingsOpened();
        }
    });

    // 11: 光影设置（由光影包列表卡片按钮进入，非侧边栏页）
    m_shaderSettingsPage = new ShaderPackSettingsPage(this);
    m_contentStack->addWidget(m_shaderSettingsPage); // index 11
    connect(m_shaderPacksPage, &InstanceFilePage::shaderSettingsRequested, this,
            [this](const QString &packPath) {
        if (m_currentInstancePath.isEmpty())
            return;
        if (m_shaderSettingsPage)
        {
            // 先切换到光影设置页，再异步解析，避免在隐藏页上执行
            // window()->grab() 整窗抓图导致 UI 卡死
            m_contentStack->setCurrentWidget(m_shaderSettingsPage);
            m_shaderSettingsPage->setCurrentPack(packPath, m_currentInstancePath);
            // 通知主窗口更新 TopBar 返回按钮与标题
            emit shaderSettingsOpened();
        }
    });

    // 12: 投影编辑（由投影列表卡片编辑按钮/双击进入，非侧边栏页）
    m_projectionEditPage = new ProjectionEditPage(this);
    m_contentStack->addWidget(m_projectionEditPage); // index 12

    // 13: 日志
    m_logPage = new InstanceLogPage(this);
    m_contentStack->addWidget(m_logPage); // index 13
    m_lanTransferPage = new LanTransferPage(this);
    m_contentStack->addWidget(m_lanTransferPage); // index 14

    m_contentStack->setCurrentIndex(0);
    mainLayout->addWidget(m_contentStack);
}

void InstanceManagePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void InstanceManagePage::loadSubPage(int stackIndex)
{
    if (m_currentInstancePath.isEmpty())
        return;

    switch (stackIndex)
    {
    case 1: lazyApplyPath(m_instanceSettingsPage); break;
    case 2: lazyApplyPath(m_exportPage); break;
    case 3: lazyApplyPath(m_modsPage); break;
    case 4: lazyApplyPath(m_savesPage); break;
    case 5: lazyApplyPath(m_screenshotsPage); break;
    case 6: lazyApplyPath(m_resourcePacksPage); break;
    case 7: lazyApplyPath(m_shaderPacksPage); break;
    case 8: lazyApplyPath(m_serverPage); break;
    case 9: lazyApplyPath(m_projectionPage); break;
    case 13: lazyApplyPath(m_logPage); break;
    case 14: lazyApplyPath(m_lanTransferPage); break;
    default: break;
    }
}

void InstanceManagePage::refresh()
{
    // 目前仅日志页需要手动刷新（页面内不再放刷新按钮，统一走顶栏）
    if (m_contentStack && m_contentStack->currentIndex() == 13 && m_logPage)
        m_logPage->loadGameLog();
}

template <typename T>
void InstanceManagePage::lazyApplyPath(T *page)
{
    if (!page)
        return;
    if (m_loadedPaths.value(page) == m_currentInstancePath)
        return;
    m_loadedPaths.insert(page, m_currentInstancePath);
    page->setInstancePath(m_currentInstancePath);
}