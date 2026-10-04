/**
 * @file   TriviaWidget.h
 * @brief  冷知识显示组件 - 每10秒自动切换一条冷知识
 * @author BlockBox Team
 */
#ifndef TRIVIAWIDGET_H
#define TRIVIAWIDGET_H

#include <QLabel>
#include <QTimer>
#include <QWidget>

/**
 * @class TriviaWidget
 * @brief 冷知识轮播组件，每10秒自动切换显示一条冷知识
 */
class TriviaWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TriviaWidget(QWidget *parent = nullptr);
    ~TriviaWidget();

    void start();
    void stop();

private slots:
    void onTriviaTimer();

private:
    void initUI();
    void loadTriviaList();
    void showNextTrivia();

    QLabel *m_iconLabel;
    QLabel *m_triviaLabel;
    QTimer m_triviaTimer;
    QStringList m_triviaList;
    int m_currentIndex;
};

#endif // TRIVIAWIDGET_H
