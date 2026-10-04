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
 */

#pragma once

#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

class HongshiClient;
class TerracottaClient;

class MultiplayerPage : public QWidget
{
    Q_OBJECT

public:
    explicit MultiplayerPage(QWidget *parent = nullptr);
    ~MultiplayerPage() override;

private:
    void initUI();
    void buildHomePage(QWidget *page);
    void buildRoomPage(QWidget *page);
    void buildHongshiHomePage(QWidget *page);
    void buildHongshiTunnelPage(QWidget *page);

    void applyThemeStyles();
    void refreshPage();          ///< 根据当前核心/状态切换页面/刷新内容
    void refreshPlayerList();    ///< 刷新陶瓦玩家列表
    void refreshStatusBar();     ///< 刷新陶瓦延迟/时间
    void refreshInstallStatus(); ///< 刷新安装/下载状态（按当前核心分发）
    void refreshHongshiPage();       ///< 刷新红石联机页面
    void refreshHongshiInstallStatus(); ///< 刷新红石内核安装/下载状态
    void refreshTunnelStatusBar();   ///< 刷新红石隧道时间
    void populateNodeCombo();    ///< 填充红石节点下拉框

    bool hongshiActive() const;  ///< 当前是否选择红石联机核心
    void onCoreChanged(int index);

    /// 弹出对话框提示（非左下卡片）
    void showInfoDialog(const QString &title, const QString &text);

private:
    TerracottaClient *m_client;
    HongshiClient *m_hongshi;
    QStackedWidget *m_stack;

    // ---- 顶部核心选择栏 ----
    QWidget *m_topBarContainer;   ///< 核心选择 + 更新按钮容器
    QComboBox *m_coreCombo;       ///< 联机核心切换（0 陶瓦 / 1 红石）
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