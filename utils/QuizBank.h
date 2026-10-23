/**
 * @file   QuizBank.h
 * @brief  入站答题题库：加载题库资源、随机组卷、记录通过状态
 * @author BlockBox Team
 * @date   2026-10-20
 */
#ifndef QUIZBANK_H
#define QUIZBANK_H

#include <QString>
#include <QVector>

/** @brief 单道入站答题题目 */
struct QuizQuestion
{
    QString text;          ///< 题干
    QString options[4];    ///< 四个选项（A/B/C/D）
    int answerIndex = 0;   ///< 正确选项下标（0-3）
    QString explanation;   ///< 答案解析
};

/**
 * @brief 入站答题题库
 *
 * 题库资源位于 :/resources/quiz/quiz_bank.txt，格式与 方块盒子答题题库.md 一致：
 * 每道题 7 行（题干、A/B/C/D 四个选项、答案、解析），题与题之间空一行。
 * 每次答题从中随机抽取 50 道，每题 2 分，得分达到 80 分即通过。
 */
class QuizBank
{
public:
    static const int kExamQuestionCount = 50; ///< 每次答题抽取的题量
    static const int kPointsPerQuestion = 2;  ///< 每道题的分值
    static const int kPassScore = 80;         ///< 通过分数线（对应答对 40 题）

    /// 返回题库中的全部题目（第一次调用时解析资源文件并缓存）
    static QVector<QuizQuestion> allQuestions();

    /// 随机抽题组卷（题库不足时返回全部题目）
    static QVector<QuizQuestion> pickExamQuestions(int count = kExamQuestionCount);

    /// 是否已通过入站答题
    static bool hasPassed();

    /// 记录本次通过结果（保存分数与时间，便于以后查看）
    static void markPassed(int score);

    /// 上次通过时的分数，未通过过返回 0
    static int lastScore();

private:
    static void loadFromResource();
};

#endif // QUIZBANK_H
