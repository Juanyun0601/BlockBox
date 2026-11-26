#ifndef APPDIALOGBASE_H
#define APPDIALOGBASE_H

#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPixmap>
#include <QPoint>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

/**
 * @brief 软件内嵌弹窗基类：边框为全屏暗色模糊遮罩 + 居中内容，不再是系统对话框
 *
 * 子类只需把居中内容卡片（圆角、半透明）加进自己的布局即可，
 * 模糊背景抓取、暗色遮罩、定位与高 DPI 对齐都由本类处理。
 * 弹窗无关闭按钮，点击居中卡片以外的遮罩区域即可关闭。
 */
class AppDialogBase : public QDialog
{
    Q_OBJECT

public:
    explicit AppDialogBase(QWidget *parent = nullptr);

    ~AppDialogBase() override;

    /// 返回要覆盖的宿主窗口（对话框挂载的顶层窗口或活动窗口）
    QWidget *hostWindow() const;

    /// 当前所有存活的 AppDialogBase 弹窗（不含已销毁的）
    static const QList<AppDialogBase *> &activeDialogs();
    /// 最上层一个可见弹窗；没有可见弹窗时返回 nullptr。
    /// 安卓返回键用它找"该被关掉的那个弹窗"。
    static AppDialogBase *topMostVisibleDialog();

    /// 阻塞式弹出。安卓覆盖层模式下不复用 QDialog::exec 的窗口级模态逻辑
    /// （父窗口禁用、焦点接管等都基于"弹窗是顶层窗口"的假设），改用嵌套事件循环。
    int exec();

protected:
    /// 让对话框精确覆盖宿主窗口的客户端区（居中内容随之居中）
    void positionOverWindow();
    /// 抓取宿主窗口内容并生成模糊背景，同时算出正确对齐偏移
    void captureBlurBackground();

    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// 查找弹窗内的居中卡片（直接子控件中 objectName 以 "Card" 结尾）
    QWidget *findCardWidget() const;
    /// 兜底：把居中卡片的最小尺寸压回所在屏幕可用范围，防止弹窗超出屏幕
    void fitCardToScreen();

    QPixmap m_blurredBackground;
    QPoint m_blurOffset;
    QEventLoop *m_execLoop = nullptr;
    /// 弹窗显示时刻：用来忽略刚显示时的误触（安卓点图标启动的那一下触摸
    /// 会落到刚弹出的遮罩上，把弹窗直接关掉）
    QElapsedTimer m_shownAt;
};

#endif // APPDIALOGBASE_H