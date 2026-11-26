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
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
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
class QFrame;

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

    /**
     * @brief 当前已经建立的隧道地址（"域名:端口" / "IP:端口"），没建立时返回空
     *
     * 「开服」页发布到服务器导航时用它做默认地址：本机 127.0.0.1 别人连不上，
     * 有了隧道地址才是真正能发给别人的那个。
     */
    QString currentTunnelAddress() const;

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

    // ---- 玩家卡片：方块盒子账户覆盖 ----
    /** 玩家卡片上要显示的方块盒子账户（查不到时留空，照旧显示游戏名） */
    struct PlayerAccount
    {
        int id = 0;
        QString username;
        QString displayName;
        QString avatar;
        bool isValid() const { return id > 0; }
    };
    /** 判断玩家列表里的这一条是不是"自己"（房主就是 HOST，房客认本机标记或昵称） */
    bool isSelfProfile(const QString &playerName, const QString &kind, bool hosting) const;
    /** 按玩家名反查方块盒子账户：带缓存，查过没有的也记下来，避免反复请求 */
    void lookupPlayerAccount(const QString &playerName);
    /** 账户查回来以后合并成一次重绘，避免连续重建卡顿 */
    void schedulePlayerListRefresh();
    /** 进房间时报给联机核心的玩家名：登录方块盒子就报账户昵称 */
    QString localPlayerName() const;
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

    // ---- 加入房间（邀请码就地输入，不弹窗） ----
    void showJoinCard();    ///< 展开邀请码输入卡，焦点落到输入框
    void hideJoinCard();    ///< 收起邀请码输入卡
    void submitJoinCode();  ///< 校验邀请码并加入房间，出错就地提示
    /// 输入卡里的说明 / 错误文案（空文本回到默认说明）
    void setJoinHintText(const QString &text, bool error = false);

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
    /** 本机实例里带「辅助」标签的模组（本地分类名含「辅助 / 工具」） */
    QStringList instanceAuxMods() const;
    QString instanceDisplayName() const;
    /**
     * @brief 联机分享 / 发布大厅时用的显示名：该实例最近游玩的那个存档名
     *
     * 存档名优先取 level.dat 里的 LevelName，读不到用目录名；一个存档都没有时
     * 才退回实例名（见 instanceDisplayName）。
     */
    QString latestSaveDisplayName() const;

    // ---- 创建分享链接 ----
    void onCreateShareLinkClicked();
    /// 生成链接前先让房主写一句简要介绍（朋友打开邀请页时能看到），取消则返回 false
    bool promptShareIntro(QString *description);
    /// 展示生成的链接：可复制，也能直接发给论坛好友
    void showShareLinkDialog(const QString &title, const QString &link,
                             const QString &description = QString());
    /// 分享短链续期：房主联机期间每 40 秒一次，服务端 2 分钟没收到就会删掉
    void startShareHeartbeat();
    /**
     * @brief 结束当前分享链接
     * @param deleteRemote true = 顺便让服务端删掉那份房间信息（链接立刻提示联机已结束）
     * @param refreshUi    false 时不动界面（页面正在销毁）
     */
    void stopShareLink(bool deleteRemote, bool refreshUi = true);

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

    // ---- 创建分享链接 ----
    QPushButton *m_shareTerracottaBtn = nullptr;
    QPushButton *m_shareHongshiBtn = nullptr;
    QPushButton *m_shareFrpBtn = nullptr;
    QTimer *m_publishHeartbeatTimer = nullptr;
    int m_publishedRoomId = 0;
    QString m_publishedCore;
    QString m_publishedJoin;

    // ---- 分享短链（房间信息存在论坛上，链接里只有房间号） ----
    QTimer *m_shareHeartbeatTimer = nullptr; ///< 分享短链续期定时器（40 秒一次）
    QString m_shareToken;                    ///< 当前分享链接的房间号
    QString m_shareSecret;                   ///< 续期 / 删除凭据（服务端只认它）
    QString m_shareCore;                     ///< 生成链接时的联机核心
    QString m_shareJoin;                     ///< 生成链接时的邀请码 / 连接地址
    QString m_shareDescription;              ///< 生成链接时房主写的简要介绍
    bool m_shareBusy = false;                ///< 正在把房间信息传到论坛

    // ---- 当前实例上下文（发布时带上） ----
    QString m_instancePath;
    QString m_instanceVersion;
    QString m_instanceLoader;

    // ---- frp 服务商联机：首页控件 ----
    QWidget *m_frpHomePage;
    QLabel *m_frpStatusDot;       ///< 状态条小圆点
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
    QLabel *m_frpLatencyDot;      ///< 隧道状态小圆点
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
    QLabel *m_statusDot;          ///< 状态条小圆点（安装 / 运行中 / 异常）
    QLabel *m_statusLabel;        ///< 顶部状态文字
    QLabel *m_descLabel;          ///< 说明信息文字
    QPushButton *m_joinBtn;       ///< 加入房间（展开邀请码输入卡）
    QPushButton *m_createBtn;     ///< 创建房间

    // ---- 陶瓦联机：加入房间输入卡（就地输入邀请码，不弹窗） ----
    QFrame *m_joinCard;           ///< 输入卡整块
    QLineEdit *m_joinCodeEdit;    ///< 邀请码输入框
    QLabel *m_joinHint;           ///< 卡片里的说明 / 错误提示
    QPushButton *m_joinConfirmBtn;///< 卡片里的「加入」
    QPushButton *m_joinCancelBtn; ///< 卡片里的「取消」

    // ---- 陶瓦联机：房间页控件 ----
    QWidget *m_roomPage;
    QLabel *m_latencyDot;         ///< 联机状态小圆点
    QLabel *m_latencyLabel;       ///< 顶部延迟和联机时间
    QFrame *m_codeCard = nullptr; ///< 邀请码卡片整块（只给房主看，客人进房后收起）
    QLabel *m_roomCodeLabel;      ///< 大大的房间邀请码
    QPushButton *m_copyCodeBtn;   ///< 复制邀请码
    QPushButton *m_leaveBtn;      ///< 离开房间按钮
    QLabel *m_playersTitle;       ///< 玩家列表标题（带人数）
    QScrollArea *m_playersScroll;
    QWidget *m_playersContainer;
    QVBoxLayout *m_playersLayout;
    QHash<QString, PlayerAccount> m_playerAccounts; ///< 玩家名 → 方块盒子账户（查到的）
    QSet<QString> m_playerLookupPending;            ///< 正在查询的玩家名
    QSet<QString> m_playerLookupMissed;             ///< 查过但没有对应账户
    bool m_playerListRefreshQueued = false;         ///< 账户查回来后的合并刷新是否已排队

    QDateTime m_roomEnterTime;    ///< 进入房间时间

    // ---- 红石联机：首页控件 ----
    QWidget *m_hongshiHomePage;
    QLabel *m_hongshiStatusDot;   ///< 状态条小圆点
    QLabel *m_hongshiStatusLabel; ///< 状态文字
    QComboBox *m_hongshiNodeCombo;///< 服务器节点选择
    QPushButton *m_hongshiNodeRefreshBtn; ///< 刷新节点列表
    QLabel *m_hongshiNodeHint;    ///< 节点列表提示
    QSpinBox *m_hongshiPortSpin;  ///< 本地 Minecraft 端口
    QPushButton *m_hongshiStartBtn; ///< 启动/停止内核按钮

    // ---- 红石联机：隧道页控件 ----
    QWidget *m_hongshiTunnelPage;
    QLabel *m_hongshiLatencyDot;   ///< 隧道状态小圆点
    QLabel *m_hongshiLatencyLabel; ///< 隧道状态与时间
    QLabel *m_tunnelAddrLabel;     ///< 隧道地址
    QPushButton *m_copyAddrBtn;    ///< 复制地址
    QPushButton *m_hongshiStopBtn; ///< 停止联机

    QDateTime m_hongshiEnterTime;  ///< 建立隧道时间
};

