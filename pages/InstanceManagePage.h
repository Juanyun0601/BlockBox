#ifndef INSTANCEMANAGEPAGE_H
#define INSTANCEMANAGEPAGE_H

#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QHash>
#include <QPointer>

#include "../utils/mod/ModData.h"
#include "../utils/content/ContentData.h"

class InstanceGameSettingsPage;
class LanTransferPage;
class InstanceModsPage;
class InstanceFilePage;
class InstanceOverviewPage;
class ModpackExportPage;
class ServerManagePage;
class ProjectionManagePage;
class SaveSettingsPage;
class WorldMapPage;
class ShaderPackSettingsPage;
class ProjectionEditPage;
class BlurLoadingOverlay;
class InstanceLogPage;
class InstanceRecordingsPage;

class InstanceManagePage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceManagePage(QWidget *parent = nullptr);
    ~InstanceManagePage();

    void setCurrentTab(int index);

    void setCurrentInstancePath(const QString &instancePath);
    void setInstanceName(const QString &name);
    void setInstanceIcon(const QString &iconPath);
    void setGameVersion(const QString &version);
    void setLoaderInfo(const QString &loader);

    InstanceGameSettingsPage *instanceSettingsPage() const { return m_instanceSettingsPage; }
    InstanceLogPage *instanceLogPage() const { return m_logPage; }

    /** 顶栏刷新按钮 / F5 触发：刷新当前所在子页（日志页重新读取日志） */
    Q_INVOKABLE void refresh();

    /**
     * 子导航搜索框分发：把查询转发给当前内层子页（子页需实现 applyPageFilter）。
     * 切换子页（setCurrentTab）时自动重新应用已存查询。
     */
    Q_INVOKABLE void applyPageFilter(const QString &query);

signals:
    void modDetailRequested(const ModInfo &info);
    void modDownloadSearchRequested(const QString &keyword);
    /** 本地资源搜索无结果，请求前往对应资源下载页在线搜索 */
    void fileSearchRequested(ContentType type, const QString &keyword);
    /**
     * 本地资源页空状态请求「快速安装对应模组」（光影/投影/录像页）
     * @param modKey 需要安装的模组标识，由主窗口按实例加载器决定实际安装哪个
     */
    void quickInstallModRequested(const QString &modKey);
    /** 请求启动游戏（由概览页转发） */
    void launchGameRequested();
    /** 请求快捷启动游戏并进入指定存档 */
    void quickLaunchSaveRequested(const QString &saveName);
    /** 请求快捷启动游戏并自动连接到服务器 */
    void quickLaunchServerRequested(const QString &address, quint16 port);
    /** 已进入存档设置子页面（由存档列表的「存档设置」按钮触发） */
    void saveSettingsOpened();
    /** 已进入存档地表地图子页面（由存档列表的「地图」按钮触发） */
    void saveMapOpened();
    /**
     * 存档地图页进入 / 离开前台（含切到别的子页、切到别的顶栏页）。
     * 主窗口据此给旋转背景降档：地图页拖动大图 + 后台瓦片渲染时，
     * 背景动画不该再抢 UI 线程。
     */
    void worldMapActiveChanged(bool active);
    /** 已进入光影设置子页面（由光影包列表的「光影设置」按钮触发） */
    void shaderSettingsOpened();
    /** 已进入投影编辑子页面（由投影列表的编辑按钮/双击触发） */
    void projectionEditOpened();
    /** 实例名称或图标被修改（displayName 为空表示使用文件夹名） */
    void instanceInfoChanged(const QString &displayName, const QString &iconPath);
    /** 本地资源卡片点击「问问AI」：请求跳转 AI 助手并引用该资源 */
    void askAiRequested(const ResourceReference &ref);

protected:
    void resizeEvent(QResizeEvent *event) override;

    /** 只用来盯存档地图页的 Show / Hide（页面自己不用改） */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();
    /** 懒加载：进入某个标签页时才触发生成对应的子页面内容 */
    void loadSubPage(int stackIndex);
    /** 若指定子页面尚未加载当前实例路径，则为其设置实例路径 */
    template <typename T>
    void lazyApplyPath(T *page);

    QStackedWidget *m_contentStack;
    InstanceOverviewPage *m_overviewPage;
    LanTransferPage *m_lanTransferPage;
    InstanceGameSettingsPage *m_instanceSettingsPage;

    ServerManagePage *m_serverPage;
    ProjectionManagePage *m_projectionPage;
    ModpackExportPage *m_exportPage;
    InstanceModsPage *m_modsPage;
    InstanceFilePage *m_savesPage;
    InstanceFilePage *m_screenshotsPage;
    InstanceFilePage *m_resourcePacksPage;
    InstanceFilePage *m_shaderPacksPage;
    InstanceLogPage *m_logPage;
    InstanceRecordingsPage *m_recordingsPage;
    SaveSettingsPage *m_saveSettingsPage;
    WorldMapPage *m_worldMapPage;
    ShaderPackSettingsPage *m_shaderSettingsPage;
    ProjectionEditPage *m_projectionEditPage;
    QString m_currentInstancePath;
    /** 记录每个子页面当前已加载/展示的实例路径，用于判断是否需要重新加载 */
    QHash<const void *, QString> m_loadedPaths;
    /** 子导航搜索框的当前查询（切换内层子页时重新应用） */
    QString m_pageFilterQuery;
    /** 最近一次被过滤的内层子页（切走/清空查询时先恢复它） */
    QPointer<QWidget> m_lastFilteredInner;
    /** 把 m_pageFilterQuery 应用到 m_contentStack 当前子页 */
    void applyFilterToCurrentSubPage();
};

#endif // INSTANCEMANAGEPAGE_H
