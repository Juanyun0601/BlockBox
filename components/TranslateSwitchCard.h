#ifndef TRANSLATESWITCHCARD_H
#define TRANSLATESWITCHCARD_H

#include <QWidget>

class QLabel;
class QPushButton;

/**
 * @brief 页面右下角的悬浮翻译切换卡片（左 EN、右 中）
 *
 * 只负责界面与状态，不关心翻译怎么做：
 * - EN 默认选中；点击任一按钮会切换选中态并发出 modeChangeRequested(bool chinese)；
 * - 页面收到 true 时自行启动翻译，调用 setBusy(true/false) 反馈进度；
 * - 翻译完成后调用 setChineseMode(true) 落到「中」态；失败时 setChineseMode(false) 回到 EN。
 *
 * 卡片自动贴住父控件的右下角，并跟随父控件大小变化重新定位。
 */
class TranslateSwitchCard : public QWidget
{
    Q_OBJECT

public:
    explicit TranslateSwitchCard(QWidget *parent = nullptr);

    /** 翻译进行中：显示「AI 翻译中…」并禁用「中」，EN 仍可点击用于取消 */
    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }

    /** 切换选中态（不发出 modeChangeRequested） */
    void setChineseMode(bool chinese);
    bool isChineseMode() const;

signals:
    /** 用户切换语言：true=切到「中」（请开始翻译），false=切回 EN（请还原原文） */
    void modeChangeRequested(bool chinese);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void reposition();

    QLabel *m_statusLabel = nullptr;
    QPushButton *m_enBtn = nullptr;
    QPushButton *m_zhBtn = nullptr;
    bool m_busy = false;
};

#endif // TRANSLATESWITCHCARD_H
