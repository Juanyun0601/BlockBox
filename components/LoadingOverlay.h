/**
 * @file   LoadingOverlay.h
 * @brief  加载覆盖层组件类声明（性能优化版）
 * @author BlockBox Team
 * @date   2026-05-17
 */
#ifndef LOADINGOVERLAY_H
#define LOADINGOVERLAY_H

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

/**
 * @class SpinnerWidget
 * @brief 旋转加载动画组件，通过 paintEvent 绘制旋转圆弧
 *
 * 使用 QTimer 驱动旋转（而非 QPropertyAnimation），减少属性系统开销。
 * 缓存主题色，避免每帧查询 ThemeManager。
 */
class SpinnerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpinnerWidget(QWidget *parent = nullptr);

    qreal rotationAngle() const;
    void setRotationAngle(qreal angle);

    void start();
    void stop();
    bool isRunning() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateThemeColor();

    qreal m_rotationAngle;
    QColor m_cachedThemeColor;  // 缓存主题色，避免每帧查询单例
    QTimer *m_timer;
    bool m_animationsEnabled;
};

/**
 * @class LoadingOverlay
 * @brief 加载覆盖层组件，用于在页面执行长时间操作时显示进度反馈
 *
 * 性能优化说明：
 * - 不使用 QGraphicsOpacityEffect（Qt 已知性能杀手，强制离屏渲染）
 * - 不使用 QPropertyAnimation（属性系统开销），改用轻量 QTimer
 * - SpinnerWidget 缓存主题色，避免每帧查询 ThemeManager
 * - 仅在必要时启用抗锯齿
 * - 低配置模式下进一步降低帧率和关闭抗锯齿
 */
class LoadingOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit LoadingOverlay(QWidget *parent = nullptr);
    ~LoadingOverlay() override;

    void setShowCancelButton(bool show);
    void setShowProgressBar(bool show);

public slots:
    void showOverlay(const QString &status = QString());
    void hideOverlay();
    void updateProgress(int percent);
    void updateStatus(const QString &status);

signals:
    void cancelRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onCancelClicked();

private:
    void initUI();
    void initStyle();
    void updateAnimationState();

    // UI 组件
    QWidget *m_centerWidget;
    QVBoxLayout *m_centerLayout;
    SpinnerWidget *m_spinner;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QPushButton *m_cancelButton;

    // 状态
    bool m_showCancelButton;
    bool m_showProgressBar;
    bool m_animationsEnabled;
};

#endif // LOADINGOVERLAY_H
