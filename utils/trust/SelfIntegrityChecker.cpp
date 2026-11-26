/**
 * @file   SelfIntegrityChecker.cpp
 * @brief  本地文件自校验实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "utils/trust/SelfIntegrityChecker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

namespace {

/** 比对时统一用正斜杠 + 小写做键：Windows 文件名不区分大小写 */
QString pathKey(const QString &path)
{
    return TrustManifest::normalizePath(path).toLower();
}

/** 是否根层需要检查的二进制（.exe / .dll） */
bool isRootBinary(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    return suffix == QLatin1String("exe") || suffix == QLatin1String("dll");
}

/** 报告里显示的短哈希（完整值在复制出来的报告里有） */
QString shortHash(const QString &hash)
{
    if (hash.isEmpty())
        return QStringLiteral("—");
    return hash.left(16) + QStringLiteral("…");
}

QString displayValue(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("—") : value;
}

/** 弹窗里「无 / 条数」的统一写法 */
QString countText(int count)
{
    return count <= 0 ? QCoreApplication::translate("SelfIntegrityChecker", "无")
                      : QString::number(count);
}

QStringList pathsOf(const QVector<IntegrityFileResult> &files)
{
    QStringList paths;
    paths.reserve(files.size());
    for (const IntegrityFileResult &file : files)
        paths << file.path;
    return paths;
}

} // namespace

QString SelfIntegrityChecker::applicationRoot()
{
    const QString dir = QCoreApplication::applicationDirPath();
    if (!dir.isEmpty())
        return QDir::cleanPath(dir);
    return QDir::cleanPath(QDir::currentPath());
}

QString SelfIntegrityChecker::mainProgramPath()
{
    const QString path = QCoreApplication::applicationFilePath();
    if (!path.isEmpty())
        return QDir::cleanPath(path);
    return QString();
}

QString SelfIntegrityChecker::calculateSha256(const QString &filePath)
{
    if (filePath.isEmpty())
        return QString();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

QString SelfIntegrityChecker::mainProgramSha256()
{
    return calculateSha256(mainProgramPath());
}

IntegrityReport SelfIntegrityChecker::check(const TrustFileManifest &manifest,
                                           const QString &rootDir)
{
    IntegrityReport report;
    report.rootDir = rootDir.isEmpty() ? applicationRoot() : QDir::cleanPath(rootDir);
    report.manifestAvailable = manifest.isValid();
    report.version = manifest.version;
    report.platform = manifest.platform;
    report.arch = manifest.arch;
    report.generatedAt = manifest.generatedAt;

    // 主程序哈希无论有没有清单都要算：清单没公示时这是用户唯一能自查的东西
    report.mainProgramPath = mainProgramPath();
    report.mainProgramSha256 = calculateSha256(report.mainProgramPath);

    if (!report.manifestAvailable)
        return report;

    const QDir root(report.rootDir);
    QHash<QString, bool> knownPaths; // 归一化路径 → 在清单里（查根层多余文件用）

    for (const TrustFileEntry &entry : manifest.files)
    {
        const QString relative = TrustManifest::normalizePath(entry.path);
        if (relative.isEmpty())
            continue;
        // 防御：清单只允许相对程序目录的路径。出现 ../ 或绝对路径说明清单不对劲
        // （或被篡改），一律跳过，避免把校验引到程序目录之外的文件上。
        if (relative.contains(QLatin1String("../")) || QDir::isAbsolutePath(relative))
            continue;
        knownPaths.insert(pathKey(relative), true);

        const QString absolute = root.absoluteFilePath(relative);
        const QFileInfo info(absolute);

        IntegrityFileResult result;
        result.path = relative;
        result.expectedSha256 = entry.sha256;
        result.expectedSize = entry.size;

        if (!info.exists() || !info.isFile())
        {
            report.missingFiles.append(result);
            continue;
        }

        result.actualSize = info.size();
        if (!entry.sha256.isEmpty())
        {
            result.actualSha256 = calculateSha256(absolute);
            if (result.actualSha256.isEmpty())
            {
                // 文件在、但读不出来（被杀毒占用 / 没权限）：单独归一类，不硬算成被改动
                report.unreadableFiles.append(relative);
                continue;
            }
            if (result.actualSha256 == entry.sha256.toLower())
                report.okFiles.append(result);
            else
                report.modifiedFiles.append(result);
            continue;
        }

        // 只公示了大小：大小不一致就按被改动处理
        if (entry.size > 0 && info.size() != entry.size)
            report.modifiedFiles.append(result);
        else
            report.okFiles.append(result);
    }

    // 清单外多出的文件：只看程序目录根层的 .exe / .dll（DLL 劫持面），不递归
    const QFileInfoList rootFiles =
        root.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : rootFiles)
    {
        if (!isRootBinary(info))
            continue;
        if (knownPaths.contains(pathKey(info.fileName())))
            continue;
        report.extraFiles.append(info.fileName());
    }

    return report;
}

QString IntegrityReport::statusText() const
{
    if (!manifestAvailable)
        return QCoreApplication::translate(
            "SelfIntegrityChecker",
            "当前版本暂未公示文件清单，已算出本机主程序的校验值。");
    if (hasAnomaly())
    {
        const int count = modifiedFiles.size() + missingFiles.size() + extraFiles.size()
                          + unreadableFiles.size();
        return QCoreApplication::translate(
                   "SelfIntegrityChecker",
                   "发现 %1 处异常：文件被改动 %2 个、缺失 %3 个、清单外多出 %4 个。")
            .arg(QString::number(count), QString::number(modifiedFiles.size()),
                 QString::number(missingFiles.size()), QString::number(extraFiles.size()));
    }
    return QCoreApplication::translate("SelfIntegrityChecker",
                                       "校验通过：%1 个文件与官方公示的清单一致。")
        .arg(okFiles.size());
}

QStringList IntegrityReport::detailLines(int perCategoryLimit) const
{
    QStringList lines;

    auto appendCategory = [&lines, perCategoryLimit](const QString &title,
                                                     const QStringList &items) {
        if (items.isEmpty())
            return;
        lines << QStringLiteral("%1（%2）").arg(title).arg(items.size());
        int listed = 0;
        for (const QString &item : items)
        {
            if (perCategoryLimit > 0 && listed >= perCategoryLimit)
            {
                lines << QCoreApplication::translate("SelfIntegrityChecker", "  …还有 %1 个")
                             .arg(items.size() - listed);
                break;
            }
            lines << QStringLiteral("  %1").arg(item);
            ++listed;
        }
    };

    appendCategory(QCoreApplication::translate("SelfIntegrityChecker", "被改动"),
                   pathsOf(modifiedFiles));
    appendCategory(QCoreApplication::translate("SelfIntegrityChecker", "缺失"),
                   pathsOf(missingFiles));
    appendCategory(QCoreApplication::translate("SelfIntegrityChecker", "清单外多出的文件"),
                   extraFiles);
    appendCategory(QCoreApplication::translate("SelfIntegrityChecker", "读不出来的文件"),
                   unreadableFiles);
    appendCategory(QCoreApplication::translate("SelfIntegrityChecker", "正常"),
                   pathsOf(okFiles));
    return lines;
}

QString IntegrityReport::reportText() const
{
    QStringList lines;
    lines << QCoreApplication::translate("SelfIntegrityChecker", "方块盒子 · 本地文件校验报告");
    lines << QStringLiteral("版本：%1   平台：%2 / %3")
                 .arg(displayValue(version), displayValue(platform), displayValue(arch));
    lines << QStringLiteral("程序目录：%1").arg(displayValue(rootDir));
    lines << QStringLiteral("校验时间：%1")
                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    if (!generatedAt.isEmpty())
        lines << QStringLiteral("清单生成时间：%1").arg(generatedAt);
    lines << QStringLiteral("本机主程序：%1").arg(displayValue(mainProgramPath));
    lines << QStringLiteral("主程序 SHA-256：%1")
                 .arg(mainProgramSha256.isEmpty()
                          ? QCoreApplication::translate("SelfIntegrityChecker", "读取失败")
                          : mainProgramSha256);
    lines << QStringLiteral("结论：%1").arg(statusText());
    lines << QString();

    auto appendResults = [&lines](const QString &title,
                                  const QVector<IntegrityFileResult> &items) {
        lines << QStringLiteral("【%1】%2").arg(title).arg(countText(items.size()));
        for (const IntegrityFileResult &file : items)
        {
            if (file.expectedSha256.isEmpty() && file.actualSha256.isEmpty())
                lines << QStringLiteral("  %1").arg(file.path);
            else
                lines << QStringLiteral("  %1\n      清单 %2 / 本机 %3")
                             .arg(file.path, shortHash(file.expectedSha256),
                                  shortHash(file.actualSha256));
        }
    };

    appendResults(QCoreApplication::translate("SelfIntegrityChecker", "被改动"), modifiedFiles);
    appendResults(QCoreApplication::translate("SelfIntegrityChecker", "缺失"), missingFiles);

    lines << QStringLiteral("【%1】%2")
                 .arg(QCoreApplication::translate("SelfIntegrityChecker", "清单外多出的文件"),
                      countText(extraFiles.size()));
    for (const QString &file : extraFiles)
        lines << QStringLiteral("  %1").arg(file);

    lines << QStringLiteral("【%1】%2")
                 .arg(QCoreApplication::translate("SelfIntegrityChecker", "读不出来的文件"),
                      countText(unreadableFiles.size()));
    for (const QString &file : unreadableFiles)
        lines << QStringLiteral("  %1").arg(file);

    appendResults(QCoreApplication::translate("SelfIntegrityChecker", "正常"), okFiles);

    return lines.join(QLatin1Char('\n'));
}
