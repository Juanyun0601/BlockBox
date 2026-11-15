/**
 * @file   BedrockMultiplayerPage.h
 * @brief  基岩版实例助手 - 联机页面
 * @author BlockBox Team
 *
 * 页面结构（参考 Java 版 MultiplayerPage 与《实例助手联机功能方案.md》）：
 *   联机首页：核心选择框（GravityCone）+ 更新按钮 + 说明文字 + 加入/创建房间按钮
 *   联机任务页：联机状态与时长 + 邀请码 + 复制按钮 + 服务器地址 + 玩家列表
 */

#pragma once

#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

class QFrame;
class GravityConeClient;

class BedrockMultiplayerPage : public QWidget
{
    Q_OBJECT

public:
    explicit BedrockMultiplayerPage(QWidget *parent = nullptr);
    ~BedrockMultiplayerPage() override;

private:
    void initUI();
    void buildHomePage(QWidget *page);
    void buildRoomPage(QWidget *page);

    void applyThemeStyles();
    void refreshPage();          ///< 根据当前状态切换页面/刷新内容
    void refreshPlayerList();    ///< 刷新玩家列表
    void refreshStatusBar();     ///< 刷新联机状态/时长/地址
    void refreshInstallStatus(); ///< 刷新安装/下载状态

    /// 进房间时报的玩家名：优先用当前登录账户的昵称，没登录才退回通用名字
    QString localPlayerName() const;

    // ---- 加入房间（邀请码就地输入，不弹窗） ----
    void showJoinCard();    ///< 展开邀请码输入卡，焦点落到输入框
    void hideJoinCard();    ///< 收起邀请码输入卡
    void submitJoinCode();  ///< 校验邀请码并加入房间，出错就地提示
    /// 输入卡里的说明 / 错误文案（空文本回到默认说明）
    void setJoinHintText(const QString &text, bool error = false);

private:
    GravityConeClient *m_client;
    QStackedWidget *m_stack;

    // ---- 顶部核心栏 ----
    QWidget *m_topBar;
    QLabel *m_coreLabel;          ///< 固定核心名（GravityCone）
    QPushButton *m_updateBtn;     ///< 更新联机核心按钮

    // ---- 首页控件 ----
    QWidget *m_homePage;
    QLabel *m_statusDot;          ///< 状态条小圆点
    QLabel *m_statusLabel;        ///< 顶部状态文字
    QLabel *m_progressHint;       ///< 核心推送的建房/进房进度（就地显示，平时隐藏）
    QLabel *m_descLabel;          ///< 说明信息文字
    QPushButton *m_joinBtn;       ///< 加入房间（展开邀请码输入卡）
    QPushButton *m_createBtn;     ///< 创建房间

    // ---- 加入房间输入卡（就地输入邀请码，不弹窗） ----
    QFrame *m_joinCard;           ///< 输入卡整块
    QLineEdit *m_joinCodeEdit;    ///< 邀请码输入框
    QLabel *m_joinHint;           ///< 卡片里的说明 / 错误提示
    QPushButton *m_joinConfirmBtn;///< 卡片里的「加入」
    QPushButton *m_joinCancelBtn; ///< 卡片里的「取消」

    // ---- 房间页控件 ----
    QWidget *m_roomPage;
    QLabel *m_latencyDot;         ///< 联机状态小圆点
    QLabel *m_latencyLabel;       ///< 顶部联机状态/时长
    QWidget *m_addressCard;       ///< 服务器地址卡片（没地址时整块隐藏）
    QLabel *m_addressLabel;       ///< 服务器地址（房客模式提示在游戏中输入的地址）
    QLabel *m_roomCodeLabel;      ///< 大大的房间邀请码
    QPushButton *m_copyCodeBtn;   ///< 复制邀请码
    QPushButton *m_leaveBtn;      ///< 离开房间按钮
    QLabel *m_playersTitle;       ///< 玩家列表标题（带人数）
    QScrollArea *m_playersScroll;
    QWidget *m_playersContainer;
    QVBoxLayout *m_playersLayout;

    QDateTime m_roomEnterTime;    ///< 进入房间时间
};
