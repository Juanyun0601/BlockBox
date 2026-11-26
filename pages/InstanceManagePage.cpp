#include "InstanceManagePage.h"

#include <QLabel>
#include <QEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>

#include "components/BlurLoadingOverlay.h"
#include "components/DataPackManagerDialog.h"
#include "components/WorldConvertDialog.h"
#include "components/WorldBackupDialog.h"
#include "components/WorldBackupCenterDialog.h"
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
#include "pages/WorldMapEntryPage.h"
#include "pages/ShaderPackSettingsPage.h"
#include "pages/InstanceLogPage.h"
#include "pages/InstanceRecordingsPage.h"
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
    , m_datapacksPage(nullptr)
    , m_logPage(nullptr)
    , m_recordingsPage(nullptr)
    , m_saveSettingsPage(nullptr)
    , m_worldMapEntryPage(nullptr)
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
    //            13=日志, 14=文件传输, 15=存档地图（非侧边栏页，仅由存档列表进入），
    //            16=录像, 17=数据包
    // 侧边栏"修改"子项已改为直接前往安装新实例页进入修改模式，不再对应本页标签
    // 侧边栏子项: 0=概览 1=设置 2=修改 3=导出整合包 4=文件传输 5=存档 6=服务器
    //            7=投影 8=截图 9=录像 10=模组 11=资源包 12=光影包 13=数据包 14=日志
    static const int tabMap[] = {0, 1, -1, 2, 14, 4, 8, 9, 5, 16, 3, 6, 7, 17, 13};
    if (index < 0 || index > 14)
        return;
    const int stackIndex = tabMap[index];
    if (stackIndex < 0)
        return;
    m_contentStack->setCurrentIndex(stackIndex);
    // 懒加载：进入对应标签页时才生成该页内容
    loadSubPage(stackIndex);
    // 子导航搜索词保持生效：应用到新切换到的子页
    applyFilterToCurrentSubPage();
    // 下载过投影但当时不在投影页：现在切过来了，再重扫目录
    if (stackIndex == 9 && m_projectionPage && m_projectionListStale)
    {
        m_projectionListStale = false;
        m_projectionPage->refresh();
    }
}

void InstanceManagePage::setCurrentInstancePath(const QString &instancePath)
{
    m_currentInstancePath = instancePath;
    // 新实例的投影列表本来就会重新加载，旧的「待重扫」标记作废
    m_projectionListStale = false;

    // 存档设置页可能残留上个实例的信息，切换实例时重置
    if (m_saveSettingsPage)
        m_saveSettingsPage->reset();
    // 地图页可能残留上个实例的地表地图
    if (m_worldMapEntryPage)
    {
        m_worldMapEntryPage->reset();
        m_worldMapEntryPage->setInstancePath(instancePath);
    }
    // 光影设置页可能残留上个实例的信息，切换实例时重置
    if (m_shaderSettingsPage)
        m_shaderSettingsPage->reset();
    // 投影编辑页可能残留上个实例的信息，切换实例时重置
    if (m_projectionEditPage)
    {
        m_projectionEditPage->setInstancePath(instancePath);
        m_projectionEditPage->reset();
    }

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
    // 记下实例版本：数据包管理弹窗用它兜底判断数据包兼容性
    m_gameVersion = version;
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
    // 13=日志, 14=文件传输
    // 15=存档地图（非侧边栏页，仅由存档列表进入）
    // 16=录像
    // 17=数据包

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
        setCurrentTab(10); // 模组
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
    connect(m_modsPage, &InstanceModsPage::askAiRequested,
            this, &InstanceManagePage::askAiRequested);

    // 4: 存档
    m_savesPage = new InstanceFilePage(InstanceFilePage::Save, this);
    m_contentStack->addWidget(m_savesPage); // index 4
    connect(m_savesPage, &InstanceFilePage::quickLaunchSaveRequested,
            this, &InstanceManagePage::quickLaunchSaveRequested);
    connect(m_savesPage, &InstanceFilePage::fileSearchRequested,
            this, [this](const QString &keyword) {
        emit fileSearchRequested(ContentType::World, keyword);
    });
    connect(m_savesPage, &InstanceFilePage::askAiRequested,
            this, &InstanceManagePage::askAiRequested);

    // 5: 截图
    m_screenshotsPage = new InstanceFilePage(InstanceFilePage::Screenshot, this);
    m_contentStack->addWidget(m_screenshotsPage); // index 5
    connect(m_screenshotsPage, &InstanceFilePage::askAiRequested,
            this, &InstanceManagePage::askAiRequested);

    // 6: 资源包
    m_resourcePacksPage = new InstanceFilePage(InstanceFilePage::ResourcePack, this);
    m_contentStack->addWidget(m_resourcePacksPage); // index 6
    connect(m_resourcePacksPage, &InstanceFilePage::fileSearchRequested,
            this, [this](const QString &keyword) {
        emit fileSearchRequested(ContentType::ResourcePack, keyword);
    });
    connect(m_resourcePacksPage, &InstanceFilePage::askAiRequested,
            this, &InstanceManagePage::askAiRequested);

    // 7: 光影包
    m_shaderPacksPage = new InstanceFilePage(InstanceFilePage::ShaderPack, this);
    m_contentStack->addWidget(m_shaderPacksPage); // index 7
    connect(m_shaderPacksPage, &InstanceFilePage::fileSearchRequested,
            this, [this](const QString &keyword) {
        emit fileSearchRequested(ContentType::ShaderPack, keyword);
    });
    connect(m_shaderPacksPage, &InstanceFilePage::askAiRequested,
            this, &InstanceManagePage::askAiRequested);
    connect(m_shaderPacksPage, &InstanceFilePage::quickInstallModRequested,
            this, &InstanceManagePage::quickInstallModRequested);

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
            // 材料清单中文名依赖当前实例
            m_projectionEditPage->setInstancePath(m_currentInstancePath);
            m_projectionEditPage->setCurrentProjection(filePath);
            // 通知主窗口更新 TopBar 返回按钮与标题
            emit projectionEditOpened();
        }
    });
    connect(m_projectionPage, &ProjectionManagePage::quickInstallModRequested,
            this, &InstanceManagePage::quickInstallModRequested);

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

    // 15: 存档地表地图（由存档列表卡片「地图」按钮进入，非侧边栏页）
    m_worldMapEntryPage = new WorldMapEntryPage(this);
    m_contentStack->addWidget(m_worldMapEntryPage); // index 15
    // 地图页真上了前台（也含整页被切走、被顶栏切走）时通知主窗口给背景降档；
    // 用事件过滤盯 Show/Hide，地图页自身不用关心背景的事
    m_worldMapEntryPage->installEventFilter(this);
    connect(m_savesPage, &InstanceFilePage::saveMapRequested, this,
            [this](const QString &saveName) {
        if (m_currentInstancePath.isEmpty())
            return;
        if (m_worldMapEntryPage)
        {
            // 先切换到地图页再渲染，避免在隐藏页上执行
            m_contentStack->setCurrentWidget(m_worldMapEntryPage);
            m_worldMapEntryPage->setCurrentSave(
                m_currentInstancePath + QStringLiteral("/saves/") + saveName, saveName);
            // 通知主窗口更新 TopBar 返回按钮与标题
            emit saveMapOpened();
        }
    });

    // 存档版本转换弹窗（由存档列表卡片「版本转化」按钮打开，Java ⇄ 基岩互转）
    connect(m_savesPage, &InstanceFilePage::saveConvertRequested, this,
            [this](const QString &saveName) {
        if (m_currentInstancePath.isEmpty())
            return;
        const QString worldDir =
            m_currentInstancePath + QStringLiteral("/saves/") + saveName;
        auto *dlg = new WorldConvertDialog(worldDir, saveName,
                                           m_currentInstancePath, window());
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->exec();
    });

    // 存档备份与恢复弹窗（由存档列表卡片「备份与恢复」按钮、存档设置页按钮打开）
    auto openWorldBackup = [this](const QString &saveName) {
        if (m_currentInstancePath.isEmpty() || saveName.isEmpty())
            return;
        WorldBackupDialog::openForWorld(this, m_currentInstancePath, saveName);
    };
    connect(m_savesPage, &InstanceFilePage::saveBackupRequested, this, openWorldBackup);
    connect(m_saveSettingsPage, &SaveSettingsPage::backupRequested, this, openWorldBackup);
    connect(m_savesPage, &InstanceFilePage::backupCenterRequested, this, [this]() {
        if (m_currentInstancePath.isEmpty())
            return;
        WorldBackupCenterDialog::openForInstance(this, m_currentInstancePath);
    });

    // 存档数据包管理弹窗（由存档列表卡片「数据包」按钮打开）
    connect(m_savesPage, &InstanceFilePage::saveDatapacksRequested, this,
            [this](const QString &saveName) {
        if (m_currentInstancePath.isEmpty() || saveName.isEmpty())
            return;
        DataPackManagerDialog::openForWorld(this, m_currentInstancePath, saveName,
                                           m_gameVersion);
    });

    // 16: 录像（Replay Mod / Flashback 录像列表）
    m_recordingsPage = new InstanceRecordingsPage(this);
    m_contentStack->addWidget(m_recordingsPage); // index 16
    connect(m_recordingsPage, &InstanceRecordingsPage::quickInstallModRequested,
            this, &InstanceManagePage::quickInstallModRequested);
    // 录像卡片「一键加入」：与服务器管理页共用同一条快捷进服链路
    connect(m_recordingsPage, &InstanceRecordingsPage::quickJoinServerRequested,
            this, &InstanceManagePage::quickLaunchServerRequested);

    // 17: 数据包（实例 datapacks 目录：在线下载的数据包在这里统一管理、检查更新，
    //     也可以从这里导入到某个存档）
    m_datapacksPage = new InstanceFilePage(InstanceFilePage::DataPack, this);
    m_contentStack->addWidget(m_datapacksPage); // index 17
    connect(m_datapacksPage, &InstanceFilePage::fileSearchRequested,
            this, [this](const QString &keyword) {
        emit fileSearchRequested(ContentType::DataPack, keyword);
    });
    connect(m_datapacksPage, &InstanceFilePage::askAiRequested,
            this, &InstanceManagePage::askAiRequested);

    m_contentStack->setCurrentIndex(0);
    mainLayout->addWidget(m_contentStack);
}

void InstanceManagePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

bool InstanceManagePage::eventFilter(QObject *watched, QEvent *event)
{
    // 存档地图页真正可见 / 不可见（Qt 会把 Show/Hide 派发给所有子控件：
    // 换内层子页、切顶栏页、窗口最小化都会走到这里）
    if (watched == m_worldMapEntryPage
        && (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        emit worldMapActiveChanged(event->type() == QEvent::Show);
    }
    return QWidget::eventFilter(watched, event);
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
    case 16: lazyApplyPath(m_recordingsPage); break;
    case 17: lazyApplyPath(m_datapacksPage); break;
    default: break;
    }
}

void InstanceManagePage::refresh()
{
    // 目前仅日志页需要手动刷新（页面内不再放刷新按钮，统一走顶栏）
    if (m_contentStack && m_contentStack->currentIndex() == 13 && m_logPage)
        m_logPage->loadGameLog();
    // 服务器页：顶栏刷新 = 重新扫描游戏内添加的服务器
    if (m_contentStack && m_contentStack->currentIndex() == 8 && m_serverPage)
        m_serverPage->refresh();
    // 存档地图页：顶栏刷新 = 重新渲染当前维度
    if (m_contentStack && m_contentStack->currentIndex() == 15 && m_worldMapEntryPage)
        m_worldMapEntryPage->refresh();
    // 录像页：顶栏刷新 = 重新扫描录像文件
    if (m_contentStack && m_contentStack->currentIndex() == 16 && m_recordingsPage)
        m_recordingsPage->refreshFileList();
    // 数据包页：顶栏刷新 = 重新扫描 datapacks 目录
    if (m_contentStack && m_contentStack->currentIndex() == 17 && m_datapacksPage)
        m_datapacksPage->refreshFileList();
    // 投影页：顶栏刷新 = 重新扫描 schematics 目录
    if (m_contentStack && m_contentStack->currentIndex() == 9 && m_projectionPage)
        m_projectionPage->refresh();
}

void InstanceManagePage::refreshProjectionPage()
{
    if (!m_projectionPage)
        return;
    // 正停在投影页就当场重扫；不在这一页只记个标记，
    // 等切过去时再重扫，避免刚下完文件就在后台解析整个 schematics 目录
    if (m_contentStack && m_contentStack->currentIndex() == 9)
        m_projectionPage->refresh();
    else
        m_projectionListStale = true;
}

void InstanceManagePage::applyPageFilter(const QString &query)
{
    m_pageFilterQuery = query;
    applyFilterToCurrentSubPage();
}

void InstanceManagePage::applyFilterToCurrentSubPage()
{
    if (!m_contentStack)
        return;

    // 先恢复上一个被过滤的子页（可能已切走）
    if (m_lastFilteredInner)
    {
        QMetaObject::invokeMethod(m_lastFilteredInner, "applyPageFilter",
                                  Q_ARG(QString, QString()));
        m_lastFilteredInner = nullptr;
    }

    if (m_pageFilterQuery.isEmpty())
        return;

    QWidget *inner = m_contentStack->currentWidget();
    if (!inner)
        return;
    // 子页实现了 Q_INVOKABLE applyPageFilter(const QString &) 才转发；
    // 未实现的子页（概览/导出/日志等）静默忽略。
    if (inner->metaObject()->indexOfMethod("applyPageFilter(QString)") < 0)
        return;
    QMetaObject::invokeMethod(inner, "applyPageFilter", Q_ARG(QString, m_pageFilterQuery));
    m_lastFilteredInner = inner;
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
