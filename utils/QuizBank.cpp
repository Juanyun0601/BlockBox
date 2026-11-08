/**
 * @file   QuizBank.cpp
 * @brief  入站答题题库实现
 * @author BlockBox Team
 * @date   2026-10-20
 */

#include "QuizBank.h"

#include "LanguageManager.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <random>

namespace {

/// 题库资源路径（由 resources/quiz/quiz_bank*.txt 编译进 qrc）
const char kBankResourceZh[] = ":/resources/quiz/quiz_bank.txt";
const char kBankResourceEn[] = ":/resources/quiz/quiz_bank_en.txt";

QVector<QuizQuestion> g_questions;
bool g_loaded = false;

/// 去掉题干前的题号（如 "241. "）
QString stripQuestionNumber(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^\\s*\\d{1,3}\\s*[.、]\\s*"));
    QString text = line;
    text.remove(re);
    return text.trimmed();
}

/// 取 “答案：C” / “Answer: C” 冒号后的内容（中英文冒号都认）
QString afterColon(const QString &line)
{
    const int idx = line.indexOf(QRegularExpression(QStringLiteral("[：:]")));
    return idx >= 0 ? line.mid(idx + 1).trimmed() : QString();
}

bool isAnswerLine(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^answer\\s*[:：]"),
                                       QRegularExpression::CaseInsensitiveOption);
    return line.startsWith(QStringLiteral("答案")) || re.match(line).hasMatch();
}

bool isExplanationLine(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^explanation\\s*[:：]"),
                                       QRegularExpression::CaseInsensitiveOption);
    return line.startsWith(QStringLiteral("解析")) || re.match(line).hasMatch();
}

QVector<QuizQuestion> parseBankFile(const QString &resourcePath, bool *ok)
{
    QVector<QuizQuestion> questions;
    if (ok)
        *ok = false;

    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "[QuizBank]" << "无法打开题库资源:" << resourcePath;
        return questions;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString all = stream.readAll();
    file.close();

    // 题目之间以空行分隔
    const QStringList blocks = all.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")),
                                         Qt::SkipEmptyParts);
    static const QRegularExpression optionRe(QStringLiteral("^\\s*([A-Da-d])\\s*[.、]\\s*(.*)$"));

    for (const QString &rawBlock : blocks)
    {
        const QStringList lines = rawBlock.split(QLatin1Char('\n'));
        if (lines.isEmpty())
            continue;

        QuizQuestion q;
        q.text = stripQuestionNumber(lines.first());
        if (q.text.isEmpty())
            continue;

        bool hasExplanation = false;
        bool hasAnswer = false;
        for (int i = 1; i < lines.size(); ++i)
        {
            const QString line = lines.at(i).trimmed();
            if (line.isEmpty())
                continue;

            if (isAnswerLine(line))
            {
                const QString answer = afterColon(line).toUpper();
                if (answer.size() == 1 && answer.at(0) >= QLatin1Char('A')
                    && answer.at(0) <= QLatin1Char('D'))
                {
                    q.answerIndex = int(answer.at(0).unicode()) - int('A');
                    hasAnswer = true;
                }
                continue;
            }

            if (isExplanationLine(line))
            {
                q.explanation = afterColon(line);
                hasExplanation = true;
                continue;
            }

            const QRegularExpressionMatch match = optionRe.match(line);
            if (match.hasMatch())
            {
                const int idx = int(match.captured(1).toUpper().at(0).unicode()) - int('A');
                if (idx >= 0 && idx < 4)
                    q.options[idx] = match.captured(2).trimmed();
            }
        }

        bool valid = !q.text.isEmpty() && hasExplanation && hasAnswer;
        for (int i = 0; i < 4 && valid; ++i)
            valid = !q.options[i].isEmpty();
        if (!valid)
        {
            qWarning() << "[QuizBank]" << "跳过格式不完整的题目:" << q.text;
            continue;
        }
        questions.append(q);
    }

    if (ok)
        *ok = true;
    return questions;
}

/// 界面语言 → 题库语言：中文（简体/繁體）用中文题库，其余用国际版英文题库
bool preferEnglishBank()
{
    switch (LanguageManager::instance()->currentLanguage())
    {
    case LanguageManager::Chinese:
    case LanguageManager::ChineseTraditional:
        return false;
    default:
        return true;
    }
}

} // namespace

void QuizBank::loadFromResource()
{
    if (g_loaded)
        return;
    g_loaded = true;

    const bool wantEnglish = preferEnglishBank();
    bool ok = false;

    if (wantEnglish)
    {
        g_questions = parseBankFile(QString::fromLatin1(kBankResourceEn), &ok);
        if (!ok || g_questions.isEmpty())
        {
            qWarning() << "[QuizBank]" << "英文题库不可用，回退中文题库";
            ok = false;
            g_questions.clear();
        }
    }

    if (!ok || g_questions.isEmpty())
    {
        g_questions = parseBankFile(QString::fromLatin1(kBankResourceZh), &ok);
    }

    qInfo() << "[QuizBank]" << (wantEnglish ? "英文" : "中文") << "题库加载完成，共"
            << g_questions.size() << "道题";
}

QVector<QuizQuestion> QuizBank::allQuestions()
{
    if (!g_loaded)
        loadFromResource();
    return g_questions;
}

QVector<QuizQuestion> QuizBank::pickExamQuestions(int count)
{
    QVector<QuizQuestion> pool = allQuestions();
    if (pool.isEmpty())
        return pool;

    std::mt19937 engine(static_cast<unsigned int>(QRandomGenerator::global()->generate()));
    std::shuffle(pool.begin(), pool.end(), engine);

    if (count > 0 && count < pool.size())
        pool.resize(count);
    return pool;
}

bool QuizBank::hasPassed()
{
    return QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
        .value(QStringLiteral("quiz/passed"), false)
        .toBool();
}

void QuizBank::markPassed(int score)
{
    QSettings settings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"));
    settings.setValue(QStringLiteral("quiz/passed"), true);
    settings.setValue(QStringLiteral("quiz/score"), score);
    settings.setValue(QStringLiteral("quiz/passedAt"),
                      QDateTime::currentDateTime().toString(Qt::ISODate));
    settings.sync();
}

int QuizBank::lastScore()
{
    return QSettings(QStringLiteral("BlockBox"), QStringLiteral("BlockBox"))
        .value(QStringLiteral("quiz/score"), 0)
        .toInt();
}
