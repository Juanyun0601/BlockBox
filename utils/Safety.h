/**
 * @file   Safety.h
 * @brief  路径与命令行安全工具（头文件实现，供各处复用）
 * @author BlockBox Team
 *
 * 这里集中处理两类"危险输入"：
 *   1. 压缩包条目名 / 文件名 —— 可能是 "../../evil.exe" 这类越权路径；
 *   2. 拼进 PowerShell / shell 命令串的路径 —— 可能含单引号导致命令注入。
 * 所有涉及外部文件的逻辑都应先过这里，避免路径穿越与命令注入。
 */

#pragma once

#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace Safety {

/**
 * @brief 压缩包条目名是否安全（不允许绝对路径、盘符、上跳目录）
 *
 * 通过检查的条目只能落在解压目标目录之内。注意这里同时拒绝 '\'
 * 写法的上跳，Windows 上 Join-Path / QFile 都会把 '\' 当分隔符。
 */
inline bool isSafeEntryName(const QString &name)
{
    if (name.isEmpty())
        return false;
    if (name.contains(QChar(0)))
        return false;

    QString normalized = name;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // 绝对路径 / UNC 路径
    if (normalized.startsWith(QLatin1Char('/')))
        return false;
    // 盘符（C:xxx）
    if (normalized.size() >= 2 && normalized.at(1) == QLatin1Char(':'))
        return false;

    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String(".."))
            return false;
    }
    return true;
}

/**
 * @brief 判断 target 是否仍在 baseDir 之内（按词法规整后比较）
 */
inline bool isPathInside(const QString &baseDir, const QString &target)
{
    const QString base = QDir::cleanPath(baseDir);
    const QString cleaned = QDir::cleanPath(target);
    if (base.isEmpty() || cleaned.isEmpty())
        return false;
#ifdef Q_OS_WIN
    if (cleaned.compare(base, Qt::CaseInsensitive) == 0)
        return true;
    return cleaned.startsWith(base + QLatin1Char('/'), Qt::CaseInsensitive);
#else
    if (cleaned == base)
        return true;
    return cleaned.startsWith(base + QLatin1Char('/'));
#endif
}

/**
 * @brief 把外部提供的文件名压成安全的单层文件名
 *
 * 去掉目录部分与 Windows 非法字符，拒绝 "." / ".." 之类的名字。
 * 无法得到可用名字时返回 fallback。
 */
inline QString safeFileName(const QString &name, const QString &fallback)
{
    QString normalized = name;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    QString base = normalized.section(QLatin1Char('/'), -1);
    base = base.trimmed();

    QString cleaned;
    cleaned.reserve(base.size());
    for (const QChar ch : base) {
        if (ch.unicode() < 0x20)
            continue;
        switch (ch.unicode()) {
        case '<': case '>': case ':': case '"':
        case '/': case '\\': case '|': case '?': case '*':
            cleaned.append(QLatin1Char('_'));
            break;
        default:
            cleaned.append(ch);
            break;
        }
    }
    while (cleaned.endsWith(QLatin1Char('.')) || cleaned.endsWith(QLatin1Char(' ')))
        cleaned.chop(1);

    if (cleaned.isEmpty() || cleaned == QLatin1String(".") || cleaned == QLatin1String(".."))
        return fallback;
    return cleaned;
}

/**
 * @brief 把一段文本转义成 PowerShell 单引号字符串的内容
 *
 * 用法：QStringLiteral("Get-Item '%1'").arg(Safety::psQuote(path))
 * PowerShell 单引号串里只有 ' 需要写成 ''。
 */
inline QString psQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    return escaped;
}

/**
 * @brief 把一段文本转义成 POSIX shell 单引号字符串的内容
 */
inline QString shQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return escaped;
}

} // namespace Safety
