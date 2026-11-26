#ifndef GAMELAUNCHPAGE_H
#define GAMELAUNCHPAGE_H

#include "utils/GameLauncher.h"

#include <QStringList>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class LaunchLogDialog;

/**
 * @brief 安卓端整页启动界面：点「启动游戏」后进入，替代桌面版底部任务卡形式
 *
 * 居中展示「启动游戏」大标题、当前进度文字、进度条与「查看日志」按钮；
 * 日志在居中弹窗（LaunchLogDialog）中查看。游戏画面出现后由主窗口
 * 切回主页，本页随之消失（键位悬浮层、方块盒子悬浮图标照常显示）。
 */
class GameLaunchPage : public QWidget
{
    Q_OBJECT

public:
    explicit GameLaunchPage(QWidget *parent = nullptr);
    ~GameLaunchPage();

    /// 每次发起启动时调用：写日志分隔行、归零进度、进入启动中状态
    void beginLaunch();
    /// 启动整体进度（0~100）与当前步骤文字
    void setProgress(int percent, const QString &message);
    /// 启动状态变更（Failed 时自动读取 GameLauncher 的错误消息显示）
    void setLaunchStatus(GameLauncher::LaunchStatus status);
    /// 追加一条启动日志（内部缓存，供日志弹窗展示）
    void appendLog(const QString &line);

private:
    void initUI();
    void refreshStyle();
    void openLogDialog();
    void updateButtons();

    QLabel *m_iconLabel;
    QLabel *m_titleLabel;
    QLabel *m_percentLabel;
    QLabel *m_messageLabel;
    QProgressBar *m_progressBar;
    QPushButton *m_viewLogBtn;
    QPushButton *m_cancelBtn;

    GameLauncher::LaunchStatus m_status = GameLauncher::Idle;
    QStringList m_logLines;
    LaunchLogDialog *m_logDialog = nullptr;
};

#endif // GAMELAUNCHPAGE_H
