/**
 * @file   QuizDialog.cpp
 * @brief  入站答题弹窗实现
 * @author BlockBox Team
 * @date   2026-10-20
 */

#include "QuizDialog.h"

#include <QChar>
#include <QColor>
#include <QDebug>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "utils/ThemeManager.h"

namespace {

const char kOkColor[] = "#2f9e44";
const char kBadColor[] = "#e5484d";

/// 把主题色（#RRGGBB）转成带透明度的 rgba()，用于半透明底
QString rgbaOf(const QString &hex, int alpha)
{
    QColor color(hex);
    if (!color.isValid())
        color = QColor(0x4c, 0x8d, 0xff);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha);
}

} // namespace

QuizDialog::QuizDialog(QWidget *parent)
    : AppDialogBase(parent)
{
    setObjectName(QStringLiteral("blockQuizDialog"));
    setWindowTitle(tr("入站答题"));

    ThemeManager *tm = ThemeManager::instance();
    const bool isLight = (tm->currentTheme() == ThemeManager::LightTheme);
    m_accentColor = tm->currentThemeColor();
    m_accentSoft = rgbaOf(m_accentColor, 36);
    m_cardBg = isLight ? QStringLiteral("rgba(255, 255, 255, 244)")
                       : QStringLiteral("rgba(46, 46, 50, 244)");
    m_cardBorder = isLight ? QStringLiteral("rgba(210, 210, 210, 220)")
                           : QStringLiteral("rgba(92, 92, 98, 220)");
    m_textColor = isLight ? QStringLiteral("#4a4a4a") : QStringLiteral("#c9c9c9");
    m_subColor = isLight ? QStringLiteral("#8a8a8a") : QStringLiteral("#909090");
    m_okColor = QLatin1String(kOkColor);
    m_badColor = QLatin1String(kBadColor);

    initUI();
    startExam();
}

void QuizDialog::initUI()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("blockQuizCard"));
    card->setMinimumWidth(460);
    card->setMaximumWidth(620);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(30, 26, 30, 24);
    cardLayout->setSpacing(12);

    m_stack = new QStackedWidget(card);
    cardLayout->addWidget(m_stack);

    // ── 答题页 ────────────────────────────────────────────────
    QWidget *examPage = new QWidget(m_stack);
    QVBoxLayout *examLayout = new QVBoxLayout(examPage);
    examLayout->setContentsMargins(0, 0, 0, 0);
    examLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel(tr("入站答题"), examPage);
    titleLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 17px; font-weight: 700;").arg(m_textColor));
    examLayout->addWidget(titleLabel);

    QLabel *subtitleLabel = new QLabel(
        tr("从题库中随机抽取 %1 题，每题 %2 分，达到 %3 分即可使用启动器。答错后会显示正确答案。")
            .arg(QuizBank::kExamQuestionCount)
            .arg(QuizBank::kPointsPerQuestion)
            .arg(QuizBank::kPassScore),
        examPage);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;").arg(m_subColor));
    examLayout->addWidget(subtitleLabel);

    QHBoxLayout *progressRow = new QHBoxLayout();
    progressRow->setContentsMargins(0, 0, 0, 0);
    progressRow->setSpacing(12);

    m_progressLabel = new QLabel(examPage);
    m_progressLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; font-weight: 600;").arg(m_textColor));
    progressRow->addWidget(m_progressLabel, 0);

    m_progressBar = new QProgressBar(examPage);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setMinimumWidth(200);
    m_progressBar->setStyleSheet(
        QStringLiteral("QProgressBar { border: none; background-color: rgba(128, 128, 128, 80);"
                       " border-radius: 3px; }"
                       "QProgressBar::chunk { background-color: %1; border-radius: 3px; }")
            .arg(m_accentColor));
    progressRow->addWidget(m_progressBar, 1);
    examLayout->addLayout(progressRow);

    m_questionLabel = new QLabel(examPage);
    m_questionLabel->setWordWrap(true);
    m_questionLabel->setMinimumHeight(46);
    m_questionLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_questionLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 15px; font-weight: 600;").arg(m_textColor));
    examLayout->addWidget(m_questionLabel);

    QVBoxLayout *optionLayout = new QVBoxLayout();
    optionLayout->setContentsMargins(0, 0, 0, 0);
    optionLayout->setSpacing(8);

    for (int i = 0; i < 4; ++i)
    {
        QPushButton *btn = new QPushButton(examPage);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumHeight(38);
        btn->setStyleSheet(optionBaseStyle());
        connect(btn, &QPushButton::clicked, this, [this, i]() { onOptionClicked(i); });
        m_optionButtons[i] = btn;
        optionLayout->addWidget(btn);
    }
    examLayout->addLayout(optionLayout);

    m_feedbackLabel = new QLabel(examPage);
    m_feedbackLabel->setWordWrap(true);
    m_feedbackLabel->setTextFormat(Qt::RichText);
    m_feedbackLabel->setVisible(false);
    examLayout->addWidget(m_feedbackLabel);

    QHBoxLayout *footerRow = new QHBoxLayout();
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(10);

    m_correctLabel = new QLabel(examPage);
    m_correctLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;").arg(m_subColor));
    footerRow->addWidget(m_correctLabel, 0);
    footerRow->addStretch(1);

    m_exitButton = new QPushButton(tr("退出"), examPage);
    m_exitButton->setCursor(Qt::PointingHandCursor);
    m_exitButton->setMinimumHeight(34);
    m_exitButton->setStyleSheet(
        QStringLiteral("QPushButton { background-color: transparent; color: %1;"
                       " border: none; border-radius: 10px; padding: 7px 12px;"
                       " font-size: 13px; }"
                       "QPushButton:hover { background-color: %2; }")
            .arg(m_subColor, m_accentSoft));
    m_exitButton->setToolTip(tr("关闭答题，稍后可以再打开"));
    connect(m_exitButton, &QPushButton::clicked, this, &QDialog::reject);
    footerRow->addWidget(m_exitButton, 0);

    m_actionButton = new QPushButton(tr("提交答案"), examPage);
    m_actionButton->setCursor(Qt::PointingHandCursor);
    m_actionButton->setMinimumHeight(34);
    m_actionButton->setMinimumWidth(120);
    m_actionButton->setEnabled(false);
    m_actionButton->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: #ffffff; border: none;"
                       " border-radius: 10px; padding: 7px 20px; font-size: 13px; font-weight: 600; }"
                       "QPushButton:hover { background-color: %2; }"
                       "QPushButton:disabled { background-color: rgba(128, 128, 128, 70);"
                       " color: #f2f2f2; }")
            .arg(m_accentColor, rgbaOf(m_accentColor, 220)));
    connect(m_actionButton, &QPushButton::clicked, this, &QuizDialog::onSubmitClicked);
    footerRow->addWidget(m_actionButton, 0);

    examLayout->addLayout(footerRow);

    // ── 结果页 ────────────────────────────────────────────────
    QWidget *resultPage = new QWidget(m_stack);
    QVBoxLayout *resultLayout = new QVBoxLayout(resultPage);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(10);

    resultLayout->addStretch(1);

    m_scoreLabel = new QLabel(resultPage);
    m_scoreLabel->setAlignment(Qt::AlignCenter);
    resultLayout->addWidget(m_scoreLabel);

    m_resultLabel = new QLabel(resultPage);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 15px; font-weight: 700;").arg(m_textColor));
    resultLayout->addWidget(m_resultLabel);

    m_resultDetailLabel = new QLabel(resultPage);
    m_resultDetailLabel->setAlignment(Qt::AlignCenter);
    m_resultDetailLabel->setWordWrap(true);
    m_resultDetailLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;").arg(m_subColor));
    resultLayout->addWidget(m_resultDetailLabel);

    resultLayout->addStretch(1);

    QHBoxLayout *resultBtnRow = new QHBoxLayout();
    resultBtnRow->setContentsMargins(0, 8, 0, 0);
    resultBtnRow->setSpacing(12);
    resultBtnRow->addStretch(1);

    m_retryButton = new QPushButton(tr("重新答题"), resultPage);
    m_retryButton->setCursor(Qt::PointingHandCursor);
    m_retryButton->setMinimumHeight(34);
    m_retryButton->setStyleSheet(
        QStringLiteral("QPushButton { background-color: transparent; color: %1;"
                       " border: 1px solid %2; border-radius: 10px; padding: 7px 20px;"
                       " font-size: 13px; }"
                       "QPushButton:hover { background-color: %3; }")
            .arg(m_textColor, m_cardBorder, m_accentSoft));
    connect(m_retryButton, &QPushButton::clicked, this, &QuizDialog::onRestartClicked);
    resultBtnRow->addWidget(m_retryButton);

    m_finishButton = new QPushButton(tr("进入启动器"), resultPage);
    m_finishButton->setCursor(Qt::PointingHandCursor);
    m_finishButton->setMinimumHeight(34);
    m_finishButton->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: #ffffff; border: none;"
                       " border-radius: 10px; padding: 7px 20px; font-size: 13px;"
                       " font-weight: 600; }"
                       "QPushButton:hover { background-color: %2; }")
            .arg(m_accentColor, rgbaOf(m_accentColor, 220)));
    connect(m_finishButton, &QPushButton::clicked, this, &QuizDialog::onFinishClicked);
    resultBtnRow->addWidget(m_finishButton);

    resultBtnRow->addStretch(1);
    resultLayout->addLayout(resultBtnRow);

    m_stack->addWidget(examPage);
    m_stack->addWidget(resultPage);

    card->setStyleSheet(
        QStringLiteral("QWidget#blockQuizCard { background-color: %1;"
                       " border: 1px solid %2; border-radius: 16px; }")
            .arg(m_cardBg, m_cardBorder));

    rootLayout->addWidget(card, 0, Qt::AlignCenter);
}

QString QuizDialog::optionBaseStyle() const
{
    return QStringLiteral(
               "QPushButton { background-color: transparent; color: %1;"
               " border: 1px solid %2; border-radius: 10px; padding: 10px 14px;"
               " text-align: left; font-size: 13px; }"
               "QPushButton:hover { border-color: %3; background-color: %4; }"
               "QPushButton:checked { border-color: %3; background-color: %4; }"
               "QPushButton:disabled { color: %5; }")
        .arg(m_textColor, m_cardBorder, m_accentColor, m_accentSoft, m_subColor);
}

QString QuizDialog::highlightedOptionStyle(const QString &color) const
{
    QColor c(color);
    const QString soft = QStringLiteral("rgba(%1, %2, %3, 50)")
                             .arg(c.red())
                             .arg(c.green())
                             .arg(c.blue());
    return QStringLiteral(
               "QPushButton { background-color: %1; color: %2;"
               " border: 2px solid %3; border-radius: 10px; padding: 9px 13px;"
               " text-align: left; font-size: 13px; }")
        .arg(soft, m_textColor, color);
}

void QuizDialog::setOptionState(int index, const QString &state)
{
    if (index < 0 || index >= 4 || !m_optionButtons[index])
        return;
    if (state == QStringLiteral("correct"))
        m_optionButtons[index]->setStyleSheet(highlightedOptionStyle(m_okColor));
    else if (state == QStringLiteral("wrong"))
        m_optionButtons[index]->setStyleSheet(highlightedOptionStyle(m_badColor));
    else
        m_optionButtons[index]->setStyleSheet(optionBaseStyle());
}

void QuizDialog::startExam()
{
    m_questions = QuizBank::pickExamQuestions();
    m_current = 0;
    m_selected = -1;
    m_correctCount = 0;
    m_score = 0;
    m_answered = false;
    m_passed = false;

    if (m_questions.isEmpty())
    {
        // 题库资源缺失属于构建问题，不能因此把用户挡在启动器外面
        qWarning() << "[QuizDialog]" << "题库为空，跳过入站答题";
        accept();
        return;
    }

    m_stack->setCurrentIndex(0);
    showQuestion();
}

void QuizDialog::showQuestion()
{
    const QuizQuestion &q = m_questions.at(m_current);

    m_selected = -1;
    m_answered = false;

    m_progressLabel->setText(
        tr("第 %1 / %2 题").arg(m_current + 1).arg(m_questions.size()));
    m_progressBar->setMaximum(m_questions.size());
    m_progressBar->setValue(m_current + 1);
    m_correctLabel->setText(
        tr("已答对 %1 题（每题 %2 分）").arg(m_correctCount).arg(QuizBank::kPointsPerQuestion));
    m_questionLabel->setText(q.text);

    for (int i = 0; i < 4; ++i)
    {
        QPushButton *btn = m_optionButtons[i];
        btn->setText(QStringLiteral("%1. %2").arg(QChar('A' + i)).arg(q.options[i]));
        btn->setChecked(false);
        btn->setEnabled(true);
        btn->setStyleSheet(optionBaseStyle());
    }

    m_feedbackLabel->clear();
    m_feedbackLabel->setVisible(false);

    m_actionButton->setText(tr("提交答案"));
    m_actionButton->setEnabled(false);
}

void QuizDialog::onOptionClicked(int index)
{
    if (m_answered)
        return;
    m_selected = index;
    m_actionButton->setEnabled(true);
}

void QuizDialog::onSubmitClicked()
{
    if (m_answered)
    {
        // 最后一题已作答：进入结果页
        if (m_current + 1 >= m_questions.size())
        {
            m_score = m_correctCount * QuizBank::kPointsPerQuestion;
            m_passed = (m_score >= QuizBank::kPassScore);
            if (m_passed)
                QuizBank::markPassed(m_score);
            showResultPage(m_passed);
            return;
        }
        ++m_current;
        showQuestion();
        return;
    }

    if (m_selected < 0)
        return;

    m_answered = true;
    const QuizQuestion &q = m_questions.at(m_current);
    const bool correct = (m_selected == q.answerIndex);
    if (correct)
        ++m_correctCount;

    for (int i = 0; i < 4; ++i)
        m_optionButtons[i]->setEnabled(false);
    setOptionState(q.answerIndex, QStringLiteral("correct"));
    if (!correct)
        setOptionState(m_selected, QStringLiteral("wrong"));

    const QString letter = QString(QChar('A' + q.answerIndex));
    const QString explanation = q.explanation.toHtmlEscaped();
    const QString head = correct
        ? tr("回答正确！正确答案：%1").arg(letter)
        : tr("回答错误，正确答案：%1").arg(letter);
    m_feedbackLabel->setText(
        QStringLiteral("<span style=\"color:%1;font-weight:600;\">%2</span>"
                       "<br/><span style=\"color:%3;\">解析：%4</span>")
            .arg(correct ? m_okColor : m_badColor, head, m_subColor, explanation));
    m_feedbackLabel->setVisible(true);

    m_correctLabel->setText(
        tr("已答对 %1 题（每题 %2 分）").arg(m_correctCount).arg(QuizBank::kPointsPerQuestion));
    m_actionButton->setText(m_current + 1 >= m_questions.size() ? tr("查看结果")
                                                               : tr("下一题"));
    m_actionButton->setEnabled(true);
}

void QuizDialog::showResultPage(bool passed)
{
    m_stack->setCurrentIndex(1);

    const int maxScore = m_questions.size() * QuizBank::kPointsPerQuestion;
    m_scoreLabel->setText(QStringLiteral("%1 / %2").arg(m_score).arg(maxScore));
    m_scoreLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 36px; font-weight: 800;")
            .arg(passed ? m_okColor : m_badColor));

    if (passed)
    {
        m_resultLabel->setText(tr("恭喜，已通过入站答题"));
        m_resultDetailLabel->setText(tr("现在可以正常使用方块盒子启动器了。"));
    }
    else
    {
        m_resultLabel->setText(tr("很遗憾，本次没有通过"));
        m_resultDetailLabel->setText(
            tr("你答对了 %1 / %2 题。重新答一次，答对 %3 题及以上即可通过。")
                .arg(m_correctCount)
                .arg(m_questions.size())
                .arg(QuizBank::kPassScore / QuizBank::kPointsPerQuestion));
    }

    m_retryButton->setVisible(!passed);
    m_finishButton->setText(passed ? tr("进入启动器") : tr("关闭"));
}

void QuizDialog::onRestartClicked()
{
    startExam();
}

void QuizDialog::onFinishClicked()
{
    if (m_passed)
        accept();
    else
        reject();
}

void QuizDialog::keyPressEvent(QKeyEvent *event)
{
    // 答题进行中按 Esc 不关闭弹窗，避免误触退出启动器
    if (event->key() == Qt::Key_Escape)
    {
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void QuizDialog::mousePressEvent(QMouseEvent *event)
{
    // 不调用基类的“点遮罩关闭”逻辑：答题弹窗只能通过结果页按钮结束
    QDialog::mousePressEvent(event);
}
