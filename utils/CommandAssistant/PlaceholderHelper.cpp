/**
 * @file   PlaceholderHelper.cpp
 * @brief  快捷指令占位符（<xxx>）解析工具实现
 * @author BlockBox Team
 * @date   2026-09-13
 */

#include "PlaceholderHelper.h"

QList<PlaceholderToken> PlaceholderHelper::parse(const QString &text)
{
    QList<PlaceholderToken> tokens;
    int i = 0;
    const int size = text.size();
    while (i < size)
    {
        if (text.at(i) != QLatin1Char('<'))
        {
            ++i;
            continue;
        }
        const int close = text.indexOf(QLatin1Char('>'), i + 1);
        if (close < 0)
        {
            break; // 未闭合的 '<'：其后内容不再可能是占位符
        }
        const QString inner = text.mid(i + 1, close - i - 1).trimmed();
        if (!inner.isEmpty())
        {
            tokens.append(PlaceholderToken{i, close + 1, inner});
        }
        i = close + 1;
    }
    return tokens;
}

bool PlaceholderHelper::isPlaceholder(const QString &value)
{
    const QString trimmed = value.trimmed();
    return trimmed.size() >= 2
           && trimmed.startsWith(QLatin1Char('<'))
           && trimmed.endsWith(QLatin1Char('>'));
}

QString PlaceholderHelper::stripWrappers(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (isPlaceholder(trimmed))
    {
        return trimmed.mid(1, trimmed.size() - 2).trimmed();
    }
    return trimmed;
}

QString PlaceholderHelper::wrap(const QString &inner)
{
    return QLatin1Char('<') + inner + QLatin1Char('>');
}

QString PlaceholderHelper::replaceToken(const QString &text, int start, int end, const QString &newInner)
{
    if (start < 0 || end > text.size() || start >= end)
    {
        return text;
    }
    QString result = text;
    result.replace(start, end - start, wrap(newInner));
    return result;
}
