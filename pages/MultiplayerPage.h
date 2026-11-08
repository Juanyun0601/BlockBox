/**
 * @file   MultiplayerPage.h
 * @brief  联机页面 - 实例助手 > 联机
 * @author BlockBox Team
 * @date   2026-07-07
 *
 * 页面结构（参考《实例助手联机功能方案.md》）：
 *   联机首页：核心选择框 + 更新按钮 + 说明文字 + 加入/创建房间按钮
 *   联机任务页：延迟与联机时间 + 邀请码 + 复制按钮 + 玩家列表
 *
 * 支持可切换的联机核心：
 *   陶瓦联机（Terracotta）：建房/加房/邀请码/玩家列表
 *   红石联机（Hongshi）：中转节点 + 本地端口 → 隧道地址联机
 *   樱花联机（SakuraFrp）：访问密钥 + 隧道 ID → frpc 隧道地址联机
 *   OpenFrp 联机：访问密钥 + 隧道 ID → frpc 隧道地址联机
 */

#pragma once

#include "utils/frp/FrpManager.h"
#include "utils/ForumLobbyService.h"

#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#ifdef Q_OS_ANDROID
#include <QShowEvent>
#endif

class HongshiClient;
class TerracottaClient;

class MultiplayerPage : public QWidget
{
    Q_OBJECT

public:
    explicit MultiplayerPage(QWidget *parent = nullptr);
    ~MultiplayerPage() override;

#ifdef Q_OS_ANDROID
protected:
    /// 安卓：首次进入联机页时惰性初始化内置陶瓦核心（避免应用启动即拉起原生层）
    void showEvent(QShowEvent *event) override;
public:
#endif

    /**
     * @brief 注入当前实例上下文（发布到联机大厅时带上版本 / 加载器 / 模组）
     */
    void setInstanceContext(const QString &path, const QString &version, const QString &loader);

signals:
    /** 发布到联机大厅需要登录论坛账号：请求主窗口打开账户页 */
    void forumLoginRequested();

private:
    void initUI();
    void buildCorePage(QWidget *page);
    void buildHomePage(QWidget *page);
    void buildRoomPage(QWidget *page);
    void buildHongshiHomePage(QWidget *page);
    void buildHongshiTunnelPage(QWidget *page);
    void buildFrpHomePage(QWidget *page);
    void buildFrpRoomPage(QWidget *page);

    void applyThemeStyles();
    void refreshPage();          ///< 根据当前核心/状态切换页面/刷新内容
    void refreshPlayerList();    ///< 刷新陶瓦玩家列表
    void refreshStatusBar();     ///< 刷新陶瓦延迟/时间
    void refreshInstallStatus(); ///< 刷新安装/下载状态（按当前核心分发）
    void refreshHongshiPage();       ///< 刷新红石联机页面
    void refreshHongshiInstallStatus(); ///< 刷新红石内核安装/下载状态
    void refreshTunnelStatusBar();   ///< 刷新红石隧道时间
    void populateNodeCombo();    ///< 填充红石节点下拉框
    void refreshFrpPage();           ///< 刷新 frp 服务商联机页面
    void refreshFrpInstallStatus();  ///< 刷新 frpc 安装/下载状态
    void refreshFrpStatusBar();      ///< 刷新 frp 隧道时间与地址
    void updateFrpDesc();            ///< 按当前服务商刷新说明/表单内容
    void startFrpTunnel();           ///< 校验表单后启动 frpc 隧道
    void openFrpPanel();             ///< 打开当前服务商官网面板
    void pickFrpcManually();         ///< 手动选择 frpc 程序导入

    bool hongshiActive() const;  ///< 当前是否选择红石联机核心
    bool frpActive() const;      ///< 当前是否选择 frp 服务商核心（樱花/OpenFrp）
    FrpManager::Provider frpProvider() const; ///< 当前 frp 服务商
    void onCoreChanged(int index);

    // ---- 发布到联机大厅 ----
    void onPublishToLobbyClicked();
    void publishRoomWithDialog();
    bool currentPublishTarget(QString *core, QString *joinInfo, QString *address,
                              QString *error) const;
    void startPublishHeartbeat();
    void stopPublishHeartbeat(bool closeRemote, bool notify);
    void refreshPublishButtons();
    void ensurePublishStillValid();
    QStringList instanceMods() const;
    QString instanceDisplayName() const;

    /// 弹出对话框提示（非左下卡片）
    void showInfoDialog(const QString &title, const QString &text);

private:
    TerracottaClient *m_client;
    HongshiClient *m_hongshi;
    QStackedWidget *m_stack;

    // ---- 发布到联机大厅 ----
    QPushButton *m_pubTerracottaBtn = nullptr;
    QPushButton *m_pubHongshiBtn = nullptr;
    QPushButton *m_pubFrpBtn = nullptr;
    QTimer *m_publishHeartbeatTimer = nullptr;
    int m_publishedRoomId = 0;
    QString m_publishedCore;
    QString m_publishedJoin;

    // ---- 当前实例上下文（发布时带上） ----
    QString m_instancePath;
    QString m_instanceVersion;
    QString m_instanceLoader;

    // ---- frp 服务商联机：首页控件 ----
    QWidget *m_frpHomePage;
    QLabel *m_frpStatusLabel;     ///< frpc 安装/下载状态
    QLabel *m_frpDescLabel;       ///< 服务商说明（按核心切换内容）
    QLabel *m_frpKeyHint;         ///< 访问密钥字段标签
    QLineEdit *m_frpKeyEdit;      ///< 访问密钥
    QLabel *m_frpIdHint;          ///< 隧道 ID 字段标签
    QLineEdit *m_frpIdEdit;       ///< 隧道 ID
    QLabel *m_frpHintLabel;       ///< 获取指引提示
    QWidget *m_frpLinkRow;        ///< 打开面板 / 手动选择 frpc 小按钮行
    QPushButton *m_frpPanelBtn;   ///< 打开官网面板
    QPushButton *m_frpPickBtn;    ///< 手动选择 frpc
    QPushButton *m_frpStartBtn;   ///< 启动隧道

    // ---- frp 服务商联机：房间页控件 ----
    QWidget *m_frpRoomPage;
    QLabel *m_frpLatencyLabel;    ///< 顶部隧道状态与联机时间
    QLabel *m_frpAddrLabel;       ///< 大字：远程连接地址
    QPushButton *m_frpCopyBtn;    ///< 复制连接地址
    QLabel *m_frpRoomHint;        ///< 联机指引提示
    QPushButton *m_frpStopBtn;    ///< 停止联机

    QDateTime m_frpEnterTime;     ///< 启动隧道时间

    // ---- 顶部核心选择栏 ----
    QWidget *m_topBarContainer;   ///< 核心选择 + 更新按钮容器
    QComboBox *m_coreCombo;       ///< 联机核心切换（0 陶瓦 / 1 红石 / 2 樱花 / 3 OpenFrp）
    QPushButton *m_updateBtn;     ///< 更新联机核心按钮

    // ---- 陶瓦联机：首页控件 ----
    QWidget *m_homePage;
    QLabel *m_statusLabel;        ///< 顶部状态文字
    QLabel *m_descLabel;          ///< 说明信息文字
    QPushButton *m_joinBtn;       ///< 加入房间
    QPushButton *m_createBtn;     ///< 创建房间

    // ---- 陶瓦联机：房间页控件 ----
    QWidget *m_roomPage;
    QLabel *m_latencyLabel;       ///< 顶部延迟和联机时间
    QLabel *m_roomCodeLabel;      ///< 大大的房间邀请码
    QPushButton *m_copyCodeBtn;   ///< 复制邀请码
    QPushButton *m_leaveBtn;      ///< 离开房间按钮
    QScrollArea *m_playersScroll;
    QWidget *m_playersContainer;
    QVBoxLayout *m_playersLayout;

    QDateTime m_roomEnterTime;    ///< 进入房间时间

    // ---- 红石联机：首页控件 ----
    QWidget *m_hongshiHomePage;
    QLabel *m_hongshiStatusLabel; ///< 状态文字
    QComboBox *m_hongshiNodeCombo;///< 服务器节点选择
    QPushButton *m_hongshiNodeRefreshBtn; ///< 刷新节点列表
    QLabel *m_hongshiNodeHint;    ///< 节点列表提示
    QSpinBox *m_hongshiPortSpin;  ///< 本地 Minecraft 端口
    QPushButton *m_hongshiStartBtn; ///< 启动/停止内核按钮

    // ---- 红石联机：隧道页控件 ----
    QWidget *m_hongshiTunnelPage;
    QLabel *m_hongshiLatencyLabel; ///< 隧道状态与时间
    QLabel *m_tunnelAddrLabel;     ///< 隧道地址
    QPushButton *m_copyAddrBtn;    ///< 复制地址
    QPushButton *m_hongshiStopBtn; ///< 停止联机

    QDateTime m_hongshiEnterTime;  ///< 建立隧道时间
};

