/**
 * @file   UpdateProgressButton.h
 * @brief  侧边栏圆形更新进度按钮：环形进度条 + 下载图标，完成后变绿可一键安装
 * @author BlockBox Team
 * @date   2026-10-05
 *
 * 状态流转（由 UpdateChecker 的下载信号驱动）：
 *   隐藏 -> 下载中（主题色环形进度，中心下载图标）
 *        -> 下载完成（绿色环形，点击重启并安装更新）
 *        -> 下载失败（红色环形，点击重试）
 *
 * 进度呈现：
 *   - 拿到总大小后按真实比例连续生长（逐个字节平滑推进，不是整 1% 一跳）；
 *   - 总大小未知（分块传输、加速节点没给 Content-Length）或还没收到数据时，
 *     环形画成一段旋转的短弧，让人一眼看出"正在下载"而不是卡住了。
 */
#ifndef UPDATEPROGRESSBUTTON_H
#define UPDATEPROGRESSBUTTON_H

#include <QColor>
#include <QPushButton>

class QTimer;

class UpdateProgressButton : public QPushButton
{
    Q_OBJECT

public:
    explicit UpdateProgressButton(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onDownloadStarted();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();
    void onDownloadFailed(const QString &error);
    void onClicked();
    /** 下载动画节拍：让环形进度连续生长，未知进度时让短弧绕圈旋转 */
    void onAnimationTick();

private:
    enum class State {
        Hidden,      // 无下载任务，按钮隐藏
        Downloading, // 下载中：主题色环形进度
        Completed,   // 下载完成：绿色环形，点击安装
        Failed       // 下载失败：红色环形，点击重试
    };

    void syncToState();
    QColor stateColor() const;
    QColor accentColor() const;

    State m_state = State::Hidden;
    /** 已收到的比例（received / total），总大小未知时保持 0 */
    qreal m_targetFraction = 0.0;
    /** 实际画出来的比例：每帧往目标值靠一点，环形进度看起来是连续走的 */
    qreal m_displayFraction = 0.0;
    /** 未知进度时短弧的旋转角度（度） */
    qreal m_spinDegrees = 0.0;
    QTimer *m_animTimer = nullptr;
};

#endif // UPDATEPROGRESSBUTTON_H
