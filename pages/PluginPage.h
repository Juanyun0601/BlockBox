/**
 * @file   PluginPage.h
 * @brief  插件页面类声明（右侧插件显示区域）
 * @author BlockBox Team
 * @date   2026-08-05
 *
 * 页面结构：顶部操作栏（导入/制作/打开插件文件夹）
 *          + 内容区（插件总览卡片列表 / 单个插件详情 / 插件设置界面）
 *
 * 详情页支持：启用/停用切换、进入插件设置界面、检查更新、导出、查看源码、
 *           打开主页、执行插件命令（command/script/url）。
 */
#ifndef PLUGINPAGE_H
#define PLUGINPAGE_H

#include <QHash>
#include <QList>
#include <QVBoxLayout>
#include <QWidget>

#include "utils/plugin/PluginInfo.h"

class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QWidget;
class ServerStatusChecker;

class PluginPage : public QWidget
{
    Q_OBJECT

public:
    explicit PluginPage(QWidget *parent = nullptr);

    /** 显示插件总览（卡片列表） */
    void showOverview();
    /** 显示指定索引插件的详情 */
    void showPluginDetail(int index);
    /** 重新加载插件列表（监听 PluginManager::pluginsChanged 自动调用） */
    void refreshList();

signals:
    void importPluginRequested();
    void createPluginRequested();
    void removePluginRequested(const QString &id);
    void openPluginsDirRequested();
    /** 请求使用当前实例加入指定服务器（address, port） */
    void serverJoinRequested(const QString &address, quint16 port);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();
    void rebuildCards();
    void updateDetailView(const PluginInfo &info);
    void updateHeaderCount();
    QPixmap loadPluginIcon(const PluginInfo &info, int size);
    void applyTheme();
    void runCommand(const PluginInfo &info, const PluginCommand &cmd);
    /** 处理原生插件动作（kind == "native"，如选择投影并启动编辑） */
    void handleNativeCommand(const PluginInfo &info, const QString &target);
    /** 打开投影选择并在方块编辑器中编辑（含插件设置应用与默认投影同步） */
    void openProjectionInEditor(const PluginInfo &info);
    /** 重建详情页命令/动作按钮（平铺在详情卡片下方） */
    void rebuildCommands(const PluginInfo &info);
    /** 清空命令行容器（rebuildCommands / rebuildServerList 共用） */
    void clearCommandsRow();

    // ---- 插件内嵌服务器列表（kind == "server-list"） ----
    struct ServerRowData
    {
        QString host;            // 服务器地址（不含端口）
        quint16 port = 25565;    // 端口
        QString name;            // 服务器名
        QString addressText;     // 展示用地址（含端口）
        QString version;         // 版本支持
        QString desc;            // 简介（ToolTip）
        QWidget *rowWidget = nullptr;
        QLabel *statusLbl = nullptr;   // 状态点 ●/○/…
        QLabel *pingLbl = nullptr;     // 延迟
        QLabel *playersLbl = nullptr;  // 玩家数
    };
    /** 重建详情页服务器列表（读包内 data/servers.json 渲染分类+行） */
    void rebuildServerList(const PluginInfo &info);
    /** 单台服务器检测完成回调 */
    void onServerStatusReady(int row, bool ok, int pingMs, int online, int max);
    /** 一轮检测完成 */
    void onServerCheckAllDone();
    /** 服务器搜索过滤 */
    void onServerSearchChanged(const QString &text);
    /** 服务器列表加入按钮 */
    void onServerJoinClicked(int row);
    /** 服务器列表刷新按钮 */
    void onServerRefreshClicked();

    // ---- 插件内嵌界面（manifest.ui） ----
    /** 重建详情页内嵌插件界面（表单 + 动作按钮 + 结果区） */
    void rebuildUiSection(const PluginInfo &info);
    /** 按字段类型创建输入控件 */
    QWidget *createUiFieldWidget(const PluginUiField &field);
    /** 读取字段控件当前值 */
    QString uiFieldValue(const QString &key) const;
    /** 执行插件界面动作（静默执行包内脚本，输出到结果区） */
    void runUiAction(const PluginInfo &info, const PluginUiAction &action);
    /** 异步执行包内脚本并捕获输出到结果区 */
    void runScriptDetached(const PluginInfo &info, const QString &scriptRel,
                           const QStringList &extraArgs);

    // ---- 插件设置界面 ----
    /** 进入指定插件的设置界面 */
    void showSettingsPage(int index);
    /** 重建设置表单（按 manifest settings 生成控件） */
    void rebuildSettingsForm(const PluginInfo &info);
    /** 按设置项类型创建控件 */
    QWidget *createSettingField(const PluginSettingItem &item, const QString &currentValue);
    /** 读取控件当前值 */
    QString settingFieldValue(const PluginSettingItem &item) const;
    /** 保存设置 */
    void onSettingsSave();
    /** 恢复默认 */
    void onSettingsReset();

    // 头部
    QLabel *m_titleLabel;
    QLabel *m_countLabel;
    QPushButton *m_importBtn;
    QPushButton *m_createBtn;
    QPushButton *m_openDirBtn;

    // 内容
    QStackedWidget *m_stack;
    QScrollArea *m_overviewScroll;
    QWidget *m_overviewContent;
    QVBoxLayout *m_cardsLayout;
    QWidget *m_emptyState;
    QLabel *m_emptyLabel;

    QWidget *m_detailPage;
    QScrollArea *m_detailScroll;    // 详情页滚动容器
    QLabel *m_detailIcon;
    QLabel *m_detailName;
    QLabel *m_detailMeta;
    QLabel *m_detailDesc;
    QLabel *m_detailPath;
    QLabel *m_detailStyle;
    QLabel *m_detailWarn;
    QPushButton *m_enableBtn;
    QPushButton *m_settingsBtn;
    QPushButton *m_updateBtn;
    QPushButton *m_exportBtn;
    QPushButton *m_extractBtn;
    QPushButton *m_homeBtn;
    QPushButton *m_removeBtn;
    QPushButton *m_openFileBtn;

    // 详情卡片下方：命令按钮行 + 内嵌插件界面
    QWidget *m_commandsRow;
    QList<QPushButton *> m_commandBtns;
    QWidget *m_uiSection;
    QFormLayout *m_uiFormLayout;
    QHBoxLayout *m_uiActionRow;
    QPlainTextEdit *m_uiResult;
    QHash<QString, QWidget *> m_uiFields;
    QProcess *m_uiProc;

    // 插件设置界面
    QWidget *m_settingsPage;
    QScrollArea *m_settingsScroll;  // 设置页滚动容器
    QLabel *m_settingsTitle;
    QFormLayout *m_settingsFormLayout;
    QPushButton *m_settingsResetBtn;
    QPushButton *m_settingsSaveBtn;
    QHash<QString, QWidget *> m_settingFields;
    PluginInfo m_currentSettingsPlugin;

    // ---- 服务器列表（kind == "server-list"） ----
    QList<ServerRowData> m_serverRows;
    ServerStatusChecker *m_statusChecker = nullptr;
    QTimer *m_serverRefreshTimer = nullptr;
    QLineEdit *m_serverSearch = nullptr;
    QPushButton *m_serverRefreshBtn = nullptr;
    QLabel *m_serverStat = nullptr;
    int m_serverTotal = 0;
    int m_serverDone = 0;
    int m_serverOnline = 0;
    bool m_serverChecking = false;

    QList<QWidget *> m_cardWidgets;
    QList<PluginInfo> m_plugins;
    int m_currentDetailIndex = -1;
    bool m_initialized = false;
};

#endif // PLUGINPAGE_H
