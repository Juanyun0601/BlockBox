/**
 * @file   QuizDialog.h
 * @brief  入站答题弹窗声明（随机抽 50 题，每题 2 分，80 分通过）
 * @author BlockBox Team
 * @date   2026-10-20
 */
#ifndef QUIZDIALOG_H
#define QUIZDIALOG_H

#include "components/AppDialogBase.h"
#include "utils/QuizBank.h"

#include <QString>
#include <QVector>

class QLabel;
class QKeyEvent;
class QMouseEvent;
class QProgressBar;
class QPushButton;
class QStackedWidget;

/**
 * @brief 入站答题弹窗
 *
 * 覆盖主窗口的模态弹窗：从题库随机抽取 50 题，每题 2 分，
 * 答错会立即显示正确答案与解析；总分达到 80 分即通过。
 * 答题不是启动器的硬性门槛（启动时只在右下角提醒，可跳过），
 * 用户从提醒或设置里主动打开本弹窗作答，未通过可以重新答题或直接关闭。
 * 通过后把结果写入 QSettings（quiz/passed），之后启动不再提醒。
 */
class QuizDialog : public AppDialogBase
{
    Q_OBJECT

public:
    explicit QuizDialog(QWidget *parent = nullptr);

    /// 本次答题得分（0-100）
    int score() const { return m_score; }
    /// 本次答对的题数
    int correctCount() const { return m_correctCount; }

protected:
    /// 答题过程中不允许按 Esc 关闭弹窗（避免误触直接退出启动器）
    void keyPressEvent(QKeyEvent *event) override;
    /// 答题过程中点击遮罩也不会关闭弹窗
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onOptionClicked(int index);
    void onSubmitClicked();
    void onRestartClicked();
    void onFinishClicked();

private:
    void initUI();
    void startExam();
    void showQuestion();
    void showResultPage(bool passed);
    void setOptionState(int index, const QString &state);
    QString optionBaseStyle() const;
    QString highlightedOptionStyle(const QString &color) const;

    QStackedWidget *m_stack = nullptr;

    // 答题页
    QLabel *m_progressLabel = nullptr;
    QLabel *m_correctLabel = nullptr;
    QLabel *m_questionLabel = nullptr;
    QLabel *m_feedbackLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_optionButtons[4] = {nullptr, nullptr, nullptr, nullptr};
    QPushButton *m_exitButton = nullptr;
    QPushButton *m_actionButton = nullptr;

    // 结果页
    QLabel *m_scoreLabel = nullptr;
    QLabel *m_resultLabel = nullptr;
    QLabel *m_resultDetailLabel = nullptr;
    QPushButton *m_retryButton = nullptr;
    QPushButton *m_finishButton = nullptr;

    QVector<QuizQuestion> m_questions;
    int m_current = 0;       ///< 当前题目下标
    int m_selected = -1;     ///< 当前题选择的选项下标（-1 表示未选）
    int m_correctCount = 0;  ///< 已答对题数
    int m_score = 0;         ///< 本次得分
    bool m_answered = false; ///< 当前题是否已提交
    bool m_passed = false;   ///< 本次答题是否通过

    // 主题色（构造时取自 ThemeManager）
    QString m_accentColor;
    QString m_accentSoft;
    QString m_cardBg;
    QString m_cardBorder;
    QString m_textColor;
    QString m_subColor;
    QString m_okColor;
    QString m_badColor;
};

#endif // QUIZDIALOG_H
