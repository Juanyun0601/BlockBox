/**
 * @file   InstanceServerPage.h
 * @brief  实例助手 - 本地开服页面：一键在当前实例上开一台本地服务端
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 页面结构（窗口窄，单列卡片）：
 *   状态卡：状态点 + 状态文字 + 本机地址 + 在线人数 + 一键开服 / 停止 + 进游戏
 *   设置卡：服务端核心 / 端口 / 人数 / 内存 / 服务器名称 / 正版验证
 *   控制台卡：服务端输出 + 指令输入行（op / whitelist / say 等）
 *
 * 真正的准备工作与进程管理在 LocalServerManager 里，这一层只管界面与用户设置。
 */

#ifndef INSTANCESERVERPAGE_H
#define INSTANCESERVERPAGE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "utils/server/LocalServerManager.h"
#include "utils/server/ServerAdminStore.h"

class CustomCheckBox;
class McServerPinger;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QSpinBox;
class QTimer;
class QVBoxLayout;

class InstanceServerPage : public QWidget
{
    Q_OBJECT

public:
    explicit InstanceServerPage(QWidget *parent = nullptr);
    ~InstanceServerPage() override;

    /// 注入实例上下文（路径 / 版本 / 加载器），并同步到开服管理器
    void setInstanceContext(const QString &path, const QString &version, const QString &loader);

    /// 当前是否已经开服（可以进游戏）
    bool serverRunning() const;

signals:
    /// 开服成功，外部可以据此提示或刷新大厅
    void serverStarted(const QString &address);
    /// 服务器已停止
    void serverStopped();
    /// 想用当前实例直接进这台本地服（由启动器带着 --server 启动）
    void launchGameRequested(const QString &address, int port);
    /// 想让朋友远程加入：请切到「联机」页做隧道
    void multiplayerTabRequested();
    /**
     * @brief 想把当前这台服务器发布到社区「服务器导航」
     *
     * 标题 / 正文 / 标签已经按服务器信息生成好，地址给的是本机地址；
     * 窗口那层如果再知道隧道地址，会换成真正能发给别人的那个。
     */
    void publishToServerNavigationRequested(const QString &title, const QString &content,
                                            const QStringList &tags, const QString &localAddress);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onPrimaryClicked();
    void onCopyAddressClicked();
    void onAddToServerListClicked();
    void onOpenServerDirClicked();
    void onClearConsoleClicked();
    void onCopyConsoleClicked();
    void onCoreChanged(int index);
    void onWorldChanged(int index);
    void onSettingsChanged();
    void onCommandEntered();
    void onJoinGameClicked();
    void onTunnelClicked();
    void onPublishToServerNavClicked();
    void refreshOnlineInfo();
    // ---- 管理卡（白名单 / OP / 基础设置） ----
    void onWhitelistToggled(bool enabled);
    void onAddWhitelistClicked();
    void onAddOpClicked();
    void onAdminRuleChanged();

private:
    void initUI();
    QWidget *buildStatusCard();
    QWidget *buildSettingsCard();
    QWidget *buildAdminCard();
    QWidget *buildWhitelistTab();
    QWidget *buildOpTab();
    QWidget *buildRulesTab();
    QWidget *buildConsoleCard();
    void connectManager();
    void connectOnlineProbe();
    /// 切换管理卡页签（只显示对应那一页）
    void setAdminTab(int index);

    // ---- 管理卡内部 ----
    /// 服务端目录上的管理数据（白名单 / OP / 规则）
    ServerAdminStore adminStore() const;
    /// 从磁盘重新读名单与规则，刷新管理卡
    void reloadAdminData();
    /// 名单刚被服务端改写过时，稍等一会儿再读（服务端写盘有一点延迟）
    void scheduleAdminReload(int delayMs = 800);
    /// 名单刷新后核对待办：服务器真的加进去 / 移出去了吗
    void verifyPendingAdminChanges();
    void rebuildWhitelistList();
    void rebuildOpList();
    void loadRulesFromProperties();
    /// 按「正版验证 / 是否运行中」刷新两处说明文字
    void updateAdminHints();
    /// 停服且离线模式时才能直接写名单文件（正版 UUID 算不出来）
    bool canEditLists(QString *reason) const;
    bool addWhitelistEntry(const QString &name);
    // 按值传：函数里刷新列表会把 m_whitelistEntries / m_opEntries 换掉，
    // 传引用的话调用方那句 first() 到这就成悬空了
    bool removeWhitelistEntry(ServerWhitelistEntry entry);
    bool addOpEntry(const QString &name, int level);
    bool removeOpEntry(ServerOpEntry entry);
    QString opLevelText(int level) const;
    /// 小号提示：加完人 / 出错都写在这里，不弹窗打断
    void setAdminNotice(QLabel *label, const QString &text, bool error);

    /// 按管理器状态刷新状态点 / 状态文字 / 按钮 / 控件可用性
    void refreshStateVisuals();
    /// 刷新「当前实例」说明与 Forge 这类不支持时的提示
    void refreshInstanceHint();
    void appendConsole(const QString &line);
    void setStatusDot(const QString &color);
    /// 本机玩家要填的地址：没开服时按当前设置算，开服后用实际端口
    QString displayAddress() const;
    /// 按实例版本重建服务端核心下拉（Forge / NeoForge 对版本有要求）
    void rebuildCoreCombo();
    /// 重建「世界」下拉：新建世界 + 实例里的存档
    void rebuildWorldCombo();
    /// 当前选的核心在这个版本能不能用
    bool selectedCoreAvailable() const;
    /// 按当前核心刷新设置卡里的说明文字
    void updateCoreHint();
    /// 当前选的存档目录（空 = 新建世界）
    QString chosenWorldSource() const;
    void loadSettings();
    void saveSettings();
    LocalServerManager::Config configFromUi() const;
    /// 把本机地址写进实例的 servers.dat；silent=true 用于开服后自动写入
    bool writeServerListEntry(bool silent);
    /// 指令输入框的历史（上下键翻）
    void rememberCommand(const QString &command);

    LocalServerManager *m_manager = nullptr;
    McServerPinger *m_pinger = nullptr;
    QTimer *m_pingTimer = nullptr;   ///< 运行中每 5 秒探一次在线人数

    // ---- 状态卡 ----
    QLabel *m_statusDot = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_instanceLabel = nullptr;
    QLabel *m_addressLabel = nullptr;
    QLabel *m_playersLabel = nullptr;   ///< 在线人数 / 延迟
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_addToListBtn = nullptr;
    QPushButton *m_primaryBtn = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_hintLabel = nullptr;
    QWidget *m_runningActions = nullptr;  ///< 运行中才出现的「进游戏 / 远程加入」一行
    QPushButton *m_joinGameBtn = nullptr;
    QPushButton *m_tunnelBtn = nullptr;
    QPushButton *m_publishNavBtn = nullptr;  ///< 发布到社区「服务器导航」

    // ---- 设置卡 ----
    QComboBox *m_coreCombo = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QSpinBox *m_playersSpin = nullptr;
    QComboBox *m_memoryCombo = nullptr;
    QComboBox *m_worldCombo = nullptr;   ///< 用哪个存档开局（新建世界 / 实例存档）
    QLineEdit *m_motdEdit = nullptr;    ///< 服务器名称（写进 server.properties 的 motd）
    CustomCheckBox *m_onlineModeCheck = nullptr;
    QLabel *m_coreHintLabel = nullptr;

    // ---- 管理卡：白名单 / OP / 基础设置 ----
    QVBoxLayout *m_adminPagesLayout = nullptr;
    QWidget *m_whitelistPage = nullptr;
    QWidget *m_opPage = nullptr;
    QWidget *m_rulesPage = nullptr;
    QList<QPushButton *> m_adminTabBtns;   ///< 「白名单 / 管理员 / 基础设置」三个页签

    CustomCheckBox *m_whitelistToggle = nullptr;   ///< 开启白名单（写 white-list）
    QLineEdit *m_whitelistInput = nullptr;
    QPushButton *m_whitelistAddBtn = nullptr;
    QScrollArea *m_whitelistScroll = nullptr;
    QVBoxLayout *m_whitelistListLayout = nullptr;
    QLabel *m_whitelistEmpty = nullptr;
    QLabel *m_whitelistNotice = nullptr;
    QLabel *m_whitelistHint = nullptr;

    QLineEdit *m_opInput = nullptr;
    QComboBox *m_opLevelCombo = nullptr;
    QPushButton *m_opAddBtn = nullptr;
    QScrollArea *m_opScroll = nullptr;
    QVBoxLayout *m_opListLayout = nullptr;
    QLabel *m_opEmpty = nullptr;
    QLabel *m_opNotice = nullptr;
    QLabel *m_opHint = nullptr;

    QComboBox *m_gamemodeCombo = nullptr;
    QComboBox *m_difficultyCombo = nullptr;
    CustomCheckBox *m_pvpCheck = nullptr;
    CustomCheckBox *m_flightCheck = nullptr;
    QSpinBox *m_spawnProtectSpin = nullptr;
    QSpinBox *m_viewDistanceSpin = nullptr;
    QLabel *m_rulesNotice = nullptr;
    QLabel *m_rulesHint = nullptr;

    QList<ServerWhitelistEntry> m_whitelistEntries;
    QList<ServerOpEntry> m_opEntries;
    /// 正在把磁盘内容同步到界面：此时控件的 toggled / changed 不要反向写文件
    bool m_syncingAdmin = false;
    bool m_adminLoaded = false;
    bool m_adminReloadScheduled = false;
    int m_adminTab = 0;   ///< 当前页签：0=白名单 1=管理员 2=基础设置
    /**
     * @brief 运行中发过指令、还没核对结果的名字
     *
     * 服务器收下 whitelist add / op 之后会自己写名单文件，界面等它写完再读回来对一遍：
     * 名字对不上（打错了 / 正版账号不存在）就直接告诉用户，别让人以为加成功了。
     */
    QStringList m_pendingWhitelistAdds;
    QStringList m_pendingWhitelistRemoves;
    QStringList m_pendingOpAdds;
    QStringList m_pendingOpRemoves;
    /// 核对没过时再读一轮（服务端写盘偶尔比预期慢）
    bool m_adminVerifyRetry = false;

    // ---- 控制台卡 ----
    QPlainTextEdit *m_console = nullptr;
    QLineEdit *m_commandEdit = nullptr;   ///< 服务端指令输入
    QPushButton *m_commandSendBtn = nullptr;
    QPushButton *m_openDirBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_copyLogBtn = nullptr;

    // ---- 指令历史 ----
    QStringList m_commandHistory;
    int m_historyCursor = 0;   ///< == size 表示在输入新指令

    // ---- 上下文 ----
    QString m_instancePath;
    QString m_instanceVersion;
    QString m_instanceLoader;
    QString m_progressHint;   ///< 准备阶段的实时说明（正在下载服务端 45% 等）
    QString m_onlineText;     ///< 在线信息（探测失败时也保留一句说明）
    bool m_uiReady = false;   ///< 界面搭好之前不写设置，避免把默认值当成用户选择
};

#endif // INSTANCESERVERPAGE_H
