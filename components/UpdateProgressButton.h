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
 */
#ifndef UPDATEPROGRESSBUTTON_H
#define UPDATEPROGRESSBUTTON_H

#include <QPushButton>

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
    int m_percent = 0; // 0-100，总大小未知时保持 0（环形只画轨道）
};

#endif // UPDATEPROGRESSBUTTON_H
