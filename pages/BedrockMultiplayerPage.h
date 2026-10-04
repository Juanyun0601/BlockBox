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
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

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

    /// 弹出对话框提示（非左下卡片）
    void showInfoDialog(const QString &title, const QString &text);

private:
    GravityConeClient *m_client;
    QStackedWidget *m_stack;

    // ---- 首页控件 ----
    QWidget *m_homePage;
    QLabel *m_statusLabel;        ///< 顶部状态文字
    QPushButton *m_updateBtn;     ///< 更新联机核心按钮
    QLabel *m_descLabel;          ///< 说明信息文字
    QPushButton *m_joinBtn;       ///< 加入房间
    QPushButton *m_createBtn;     ///< 创建房间

    // ---- 房间页控件 ----
    QWidget *m_roomPage;
    QLabel *m_latencyLabel;       ///< 顶部联机状态/时长
    QLabel *m_addressLabel;       ///< 服务器地址（房客模式提示在游戏中输入的地址）
    QLabel *m_roomCodeLabel;      ///< 大大的房间邀请码
    QPushButton *m_copyCodeBtn;   ///< 复制邀请码
    QPushButton *m_leaveBtn;      ///< 离开房间按钮
    QScrollArea *m_playersScroll;
    QWidget *m_playersContainer;
    QVBoxLayout *m_playersLayout;

    QDateTime m_roomEnterTime;    ///< 进入房间时间
};
