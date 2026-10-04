#ifndef INSTANCEMANAGEPAGE_H
#define INSTANCEMANAGEPAGE_H

#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QHash>

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
class ShaderPackSettingsPage;
class ProjectionEditPage;
class BlurLoadingOverlay;
class InstanceLogPage;

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

signals:
    void modDetailRequested(const ModInfo &info);
    void modDownloadSearchRequested(const QString &keyword);
    /** 本地资源搜索无结果，请求前往对应资源下载页在线搜索 */
    void fileSearchRequested(ContentType type, const QString &keyword);
    /** 请求启动游戏（由概览页转发） */
    void launchGameRequested();
    /** 请求快捷启动游戏并进入指定存档 */
    void quickLaunchSaveRequested(const QString &saveName);
    /** 请求快捷启动游戏并自动连接到服务器 */
    void quickLaunchServerRequested(const QString &address, quint16 port);
    /** 已进入存档设置子页面（由存档列表的「存档设置」按钮触发） */
    void saveSettingsOpened();
    /** 已进入光影设置子页面（由光影包列表的「光影设置」按钮触发） */
    void shaderSettingsOpened();
    /** 已进入投影编辑子页面（由投影列表的编辑按钮/双击触发） */
    void projectionEditOpened();
    /** 实例名称或图标被修改（displayName 为空表示使用文件夹名） */
    void instanceInfoChanged(const QString &displayName, const QString &iconPath);

protected:
    void resizeEvent(QResizeEvent *event) override;

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
    SaveSettingsPage *m_saveSettingsPage;
    ShaderPackSettingsPage *m_shaderSettingsPage;
    ProjectionEditPage *m_projectionEditPage;
    QString m_currentInstancePath;
    /** 记录每个子页面当前已加载/展示的实例路径，用于判断是否需要重新加载 */
    QHash<const void *, QString> m_loadedPaths;
};

#endif // INSTANCEMANAGEPAGE_H
