#ifndef APPOVERLAYDIALOG_H
#define APPOVERLAYDIALOG_H

#include <QDialog>
#include <QPoint>
#include <QPixmap>

class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QVBoxLayout;
class QWidget;

/**
 * @brief 软件内嵌弹窗基类：覆盖主窗口的模糊背景 + 居中圆角卡片
 *
 * 子类只需在 cardLayout() 中放置内容并调用 applyCardStyle() 应用统一卡片样式。
 */
class AppOverlayDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AppOverlayDialog(QWidget *parent = nullptr);
    ~AppOverlayDialog() override;

    QWidget *card() const;
    QVBoxLayout *cardLayout() const;
    void setCardMinimumWidth(int width);
    void setCardMaximumWidth(int width);

    /// 统一卡片样式（圆角、背景、标题/文本/按钮），子类可在此基础上追加自己的样式
    void applyCardStyle(const QString &extraStyle = QString());

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

    /// 使对话框精确覆盖主窗口客户端区
    void positionOverWindow();
    /// 抓取主窗口内容并生成模糊背景
    void captureBlurBackground();

    QPixmap m_blurredBackground;
    QPoint m_blurOffset;
    QWidget *m_card;
    QVBoxLayout *m_cardLayout;
};

#endif // APPOVERLAYDIALOG_H
