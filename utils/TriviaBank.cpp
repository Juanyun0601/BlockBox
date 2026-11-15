/**
 * @file   TriviaBank.cpp
 * @brief  我的世界冷知识库实现
 * @author BlockBox Team
 * @date   2026-11-15
 */

#include "TriviaBank.h"

#include <QDebug>
#include <QFile>
#include <QRandomGenerator>
#include <QStringList>
#include <QTextStream>

namespace {

/// 冷知识资源路径（由 resources/trivia/mc_trivia.txt 编译进 qrc）
const char kBankResource[] = ":/resources/trivia/mc_trivia.txt";

QStringList g_triviaList;
int g_lastIndex = -1;

/// 逐行解析冷知识资源，每行一条
QStringList parseBankFile(const QString &resourcePath)
{
    QStringList list;

    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "[TriviaBank]" << "无法打开冷知识资源:" << resourcePath;
        return list;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd())
    {
        const QString line = stream.readLine().trimmed();
        if (!line.isEmpty())
            list.append(line);
    }
    file.close();

    return list;
}

} // namespace

void TriviaBank::loadFromResource()
{
    if (!g_triviaList.isEmpty())
        return;

    g_triviaList = parseBankFile(QString::fromLatin1(kBankResource));
    qInfo() << "[TriviaBank]" << "冷知识加载完成，共" << g_triviaList.size() << "条";
}

QString TriviaBank::randomTrivia()
{
    if (g_triviaList.isEmpty())
        loadFromResource();
    if (g_triviaList.isEmpty())
        return QString();

    // 避免连续两次抽到同一条
    int index = 0;
    if (g_triviaList.size() > 1)
    {
        do
        {
            index = static_cast<int>(QRandomGenerator::global()->bounded(
                static_cast<quint32>(g_triviaList.size())));
        } while (index == g_lastIndex);
    }
    g_lastIndex = index;

    return g_triviaList.at(index);
}

int TriviaBank::count()
{
    if (g_triviaList.isEmpty())
        loadFromResource();
    return g_triviaList.size();
}
