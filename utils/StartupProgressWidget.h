/**
 * @file   StartupProgressWidget.h
 * @brief  启动进度窗口类声明
 * @author BlockBox Team
 * @date   2026-05-10
 */
#ifndef STARTUPPROGRESSWIDGET_H
#define STARTUPPROGRESSWIDGET_H

#include <atomic>

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QMutex>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QVBoxLayout>
#include <QWidget>

/**
 * @class StartupProgressWidget
 * @brief 启动进度窗口类，用于显示应用启动时的进度和状态
 * 
 * StartupProgressWidget是一个线程安全的启动进度显示窗口，提供：
 * - 进度条显示（0-100%）
 * - 状态文字显示
 * - 平滑的进度动画
 * - 简洁美观的样式
 */
class StartupProgressWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int progressValue READ progressValue WRITE setProgressValue NOTIFY progressValueChanged)

public:
    /**
     * @brief 获取StartupProgressWidget的单例实例
     * @return StartupProgressWidget* 单例实例指针
     */
    static StartupProgressWidget* instance();

    /**
     * @brief 析构函数
     */
    ~StartupProgressWidget() override;

    /**
     * @brief 获取当前进度值
     * @return int 当前进度值（0-100）
     */
    int progressValue() const;

    /**
     * @brief 设置进度值（用于动画属性）
     * @param value 进度值（0-100）
     */
    void setProgressValue(int value);

    /**
     * @brief 设置是否启用动画效果
     * @param enabled true启用动画，false禁用动画
     */
    void setAnimationsEnabled(bool enabled);

    /**
     * @brief 获取动画是否启用
     * @return true表示动画已启用
     */
    bool animationsEnabled() const;

public slots:
    /**
     * @brief 更新进度和状态（线程安全）
     * @param progress 进度值（0-100）
     * @param status 状态文字
     */
    void updateProgress(int progress, const QString& status);

    /**
     * @brief 设置进度值
     * @param progress 进度值（0-100）
     */
    void setProgress(int progress);

    /**
     * @brief 设置状态文字
     * @param status 状态文字
     */
    void setStatus(const QString& status);

    /**
     * @brief 显示启动窗口
     */
    void showStartup();

    /**
     * @brief 隐藏启动窗口（带淡出动画）
     */
    void hideStartup();

signals:
    /**
     * @brief 进度值变更信号
     * @param value 新的进度值
     */
    void progressValueChanged(int value);

protected:
    /**
     * @brief 构造函数（私有，单例模式）
     * @param parent 父窗口
     */
    explicit StartupProgressWidget(QWidget *parent = nullptr);

    /**
     * @brief 重写关闭事件，防止用户手动关闭
     * @param event 关闭事件
     */
    void closeEvent(QCloseEvent *event) override;

private:
    /**
     * @brief 初始化UI
     */
    void initUI();

    /**
     * @brief 初始化样式
     */
    void initStyle();

    /**
     * @brief 初始化动画
     */
    void initAnimations();

private:
    static StartupProgressWidget* m_instance;  // 单例实例
    static QMutex m_mutex;                     // 线程安全互斥锁

    QProgressBar* m_progressBar;               // 进度条
    QLabel* m_statusLabel;                     // 状态标签
    QLabel* m_logoLabel;                       // Logo 标签

    QVBoxLayout* m_mainLayout;                 // 主布局

    QPropertyAnimation* m_progressAnimation;   // 进度动画
    QPropertyAnimation* m_fadeOutAnimation;    // 淡出动画
    QGraphicsOpacityEffect* m_opacityEffect;   // 透明度效果

    int m_progressValue;                       // 当前进度值
    QString m_currentStatus;                   // 当前状态文字
    std::atomic<bool> m_animationsEnabled;     // 动画是否启用（线程安全）
};

#endif // STARTUPPROGRESSWIDGET_H
