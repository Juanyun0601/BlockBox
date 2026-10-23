/**
 * @file   QuizReminder.h
 * @brief  右下角入站答题提醒小窗声明
 * @author BlockBox Team
 * @date   2026-10-20
 */
#ifndef QUIZREMINDER_H
#define QUIZREMINDER_H

#include <QWidget>

class QGraphicsOpacityEffect;
class QPushButton;

/**
 * @brief 启动器右下角的入站答题提醒浮层
 *
 * 每次启动时（尚未通过答题）在主窗口右下角显示一张小卡片：
 * 点「开始答题」打开 QuizDialog 作答，点「跳过」直接使用启动器。
 * 它是主窗口内的子控件（不是独立窗口），随主窗口缩放自动贴边，
 * 不会抢焦点，也不会出现在任务栏里。
 */
class QuizReminder : public QWidget
{
    Q_OBJECT

public:
    explicit QuizReminder(QWidget *host);

    /// 计算位置并显示（已通过答题时直接隐藏）
    void showReminder();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onStartClicked();
    void onSkipClicked();

private:
    void initUI();
    void placeAtHostCorner();

    QWidget *m_host = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_skipButton = nullptr;
    QGraphicsOpacityEffect *m_opacityEffect = nullptr;
    bool m_skipped = false;
    bool m_fadePlayed = false;
};

#endif // QUIZREMINDER_H
