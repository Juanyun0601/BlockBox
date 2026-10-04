#ifndef APPDIALOGBASE_H
#define APPDIALOGBASE_H

#include <QDialog>
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

    /// 返回要覆盖的宿主窗口（对话框挂载的顶层窗口或活动窗口）
    QWidget *hostWindow() const;

protected:
    /// 让对话框精确覆盖宿主窗口的客户端区（居中内容随之居中）
    void positionOverWindow();
    /// 抓取宿主窗口内容并生成模糊背景，同时算出正确对齐偏移
    void captureBlurBackground();

    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /// 查找弹窗内的居中卡片（直接子控件中 objectName 以 "Card" 结尾）
    QWidget *findCardWidget() const;

    QPixmap m_blurredBackground;
    QPoint m_blurOffset;
};

#endif // APPDIALOGBASE_H