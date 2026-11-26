/**
 * @file   CompatMatrix.cpp
 * @brief  兼容矩阵 / 实例体检聚合算法实现
 * @author BlockBox Team
 * @date   2026-11-22
 */
#include "CompatMatrix.h"

#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace
{
/** 加载器名称归一：Modrinth 用小写短名，实例侧可能是 "Fabric"/"NeoForge" */
QString normalizeLoader(const QString &loader)
{
    QString l = loader.trimmed().toLower();
    if (l == QStringLiteral("neo")) l = QStringLiteral("neoforge");
    if (l == QStringLiteral("forge") || l == QStringLiteral("fml")) l = QStringLiteral("forge");
    return l;
}
} // namespace

bool CompatMatrix::isReleaseVersion(const QString &version)
{
    static const QRegularExpression re(QStringLiteral("^\\d+\\.\\d+(\\.\\d+)?$"));
    return re.match(version).hasMatch();
}

bool CompatMatrix::supportsVersion(const CompatMatrixRow &row, const QString &gameVersion)
{
    if (gameVersion.isEmpty())
        return false;
    for (const QString &v : row.supportedGameVersions)
    {
        if (v.compare(gameVersion, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void CompatMatrix::finalizeRows(QList<CompatMatrixRow> &rows,
                                const QString &gameVersion,
                                const QString &loaderType)
{
    const QString instanceLoader = normalizeLoader(loaderType);

    for (CompatMatrixRow &row : rows)
    {
        // 加载器覆盖判断：实例是原版/未知加载器时不参与判断
        if (!instanceLoader.isEmpty() && !row.supportedLoaders.isEmpty())
        {
            row.loaderSupported = false;
            for (const QString &l : row.supportedLoaders)
            {
                if (normalizeLoader(l) == instanceLoader)
                {
                    row.loaderSupported = true;
                    break;
                }
            }
        }
        else
        {
            row.loaderSupported = true;
        }

        // 作者声明支持的最高正式版本
        QString highest;
        for (const QString &v : row.supportedGameVersions)
        {
            if (!isReleaseVersion(v))
                continue;
            if (highest.isEmpty()
                || LocalCompatibilityChecker::compareVersions(v, highest) > 0)
            {
                highest = v;
            }
        }
        row.highestSupportedVersion = highest;

        row.supportsCurrent = row.matched
                              && row.loaderSupported
                              && supportsVersion(row, gameVersion);

        if (!row.matched)
            row.status = CompatMatrixRow::StatusUnknown;
        else if (!row.loaderSupported)
            row.status = CompatMatrixRow::StatusLoaderMismatch;
        else if (row.archived)
            row.status = CompatMatrixRow::StatusArchived;
        else if (!gameVersion.isEmpty() && !row.supportsCurrent)
            row.status = CompatMatrixRow::StatusOutdated;
        else
            row.status = CompatMatrixRow::StatusOk;
    }
}

QList<CompatMatrixTarget> CompatMatrix::buildTargets(const QList<CompatMatrixRow> &rows,
                                                     const QString &current)
{
    QList<CompatMatrixRow> active;
    for (const CompatMatrixRow &row : rows)
    {
        if (row.enabled && row.matched && row.loaderSupported)
            active.append(row);
    }

    QList<CompatMatrixTarget> targets;
    if (active.isEmpty())
        return targets;

    // 版本 → 支持该版本的模组数
    QMap<QString, int> counts;
    for (const CompatMatrixRow &row : active)
    {
        for (const QString &v : row.supportedGameVersions)
        {
            if (!isReleaseVersion(v))
                continue;
            if (!current.isEmpty()
                && LocalCompatibilityChecker::compareVersions(v, current) <= 0)
            {
                continue;  // 只关心比当前更高的版本
            }
            counts[v] = counts.value(v) + 1;
        }
    }

    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
    {
        CompatMatrixTarget t;
        t.gameVersion = it.key();
        t.supported = it.value();
        t.total = active.size();
        targets.append(t);
    }

    std::sort(targets.begin(), targets.end(),
              [](const CompatMatrixTarget &a, const CompatMatrixTarget &b) {
                  return LocalCompatibilityChecker::compareVersions(a.gameVersion,
                                                                    b.gameVersion) < 0;
              });
    return targets;
}

int CompatMatrix::supportedAtVersion(const QList<CompatMatrixRow> &rows,
                                     const QString &gameVersion)
{
    if (gameVersion.isEmpty())
        return 0;
    int count = 0;
    for (const CompatMatrixRow &row : rows)
    {
        if (row.enabled && row.matched && row.loaderSupported
            && supportsVersion(row, gameVersion))
        {
            ++count;
        }
    }
    return count;
}

CompatMatrixReport CompatMatrix::buildReport(const QString &gameVersion,
                                             const QString &loaderType,
                                             const QList<CompatMatrixRow> &rows,
                                             bool networkOk,
                                             const QString &note)
{
    CompatMatrixReport report;
    report.gameVersion = gameVersion;
    report.loaderType = loaderType;
    report.networkOk = networkOk;
    report.note = note;
    report.rows = rows;

    int active = 0;
    for (const CompatMatrixRow &row : rows)
    {
        ++report.modCount;
        if (row.matched)
        {
            ++report.matchedCount;
            if (row.archived)
                ++report.archivedCount;
            if (row.status == CompatMatrixRow::StatusOutdated)
                ++report.outdatedCount;
            if (row.status == CompatMatrixRow::StatusUnknown
                || row.status == CompatMatrixRow::StatusLoaderMismatch)
            {
                ++report.unknownCount;
            }
        }
        else
        {
            ++report.unknownCount;
        }
        if (row.enabled && row.matched && row.loaderSupported)
            ++active;
    }

    report.currentSupported = supportedAtVersion(rows, gameVersion);
    report.targets = buildTargets(rows, gameVersion);
    report.activeCount = 0;
    for (const CompatMatrixRow &row : rows)
    {
        if (row.enabled && row.matched && row.loaderSupported)
            ++report.activeCount;
    }

    // 推荐：所有参与统计的模组都有对应版本的最高目标版本
    for (int i = report.targets.size() - 1; i >= 0; --i)
    {
        const CompatMatrixTarget &t = report.targets.at(i);
        if (t.complete())
        {
            report.recommendedVersion = t.gameVersion;
            report.hasRecommendation = true;
            break;
        }
    }

    if (!networkOk)
        report.unknownCount = report.modCount;

    // 「建议停在哪个版本」：不限方向，找所有参与统计的模组都支持的最高版本。
    // 当前版本跑不动时，往回退到这个版本最稳（见《调研报告》B2）。
    if (active > 0)
    {
        QMap<QString, int> allCounts;
        for (const CompatMatrixRow &row : rows)
        {
            if (!row.enabled || !row.matched || !row.loaderSupported)
                continue;
            for (const QString &v : row.supportedGameVersions)
            {
                if (isReleaseVersion(v))
                    allCounts[v] = allCounts.value(v) + 1;
            }
        }
        for (auto it = allCounts.constBegin(); it != allCounts.constEnd(); ++it)
        {
            if (it.value() < active)
                continue;
            if (report.stableVersion.isEmpty()
                || LocalCompatibilityChecker::compareVersions(it.key(), report.stableVersion) > 0)
            {
                report.stableVersion = it.key();
            }
        }
        report.stableComplete = !report.stableVersion.isEmpty();
    }

    // 模组数量为 0 或全部未收录时，不要凭空给出「建议」
    if (active == 0)
    {
        report.hasRecommendation = false;
        report.recommendedVersion.clear();
    }
    return report;
}

QString CompatMatrix::statusText(int status)
{
    switch (status)
    {
    case CompatMatrixRow::StatusOk:            return QObject::tr("当前版本可用");
    case CompatMatrixRow::StatusOutdated:      return QObject::tr("仅支持旧版本");
    case CompatMatrixRow::StatusArchived:      return QObject::tr("已停更");
    case CompatMatrixRow::StatusLoaderMismatch:return QObject::tr("加载器不符");
    default:                                   return QObject::tr("未收录");
    }
}
