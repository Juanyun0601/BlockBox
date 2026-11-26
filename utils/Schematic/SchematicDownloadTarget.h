/**
 * @file   SchematicDownloadTarget.h
 * @brief  投影下载落点工具
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 资源页「投影」列表与投影帖详情页共用同一套落点规则：
 * 有当前实例就存进 <实例>/schematics，没选实例时由调用方弹保存框。
 * 目标目录已有同名文件不覆盖，自动补 (2)、(3)…
 */
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace SchematicDownloadTarget {

/** 实例的投影目录：<实例>/schematics；实例路径为空时返回空 */
inline QString schematicsDir(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return QString();
    return QDir::cleanPath(instancePath + QStringLiteral("/schematics"));
}

/** 实例的 mods 目录里是否已有投影模组（Litematica 的 jar，含被改名为 .disabled 的） */
inline bool hasProjectionMod(const QString &instancePath)
{
    if (instancePath.isEmpty())
        return false;
    QDir modsDir(instancePath + QStringLiteral("/mods"));
    if (!modsDir.exists())
        return false;
    const QStringList files = modsDir.entryList(QDir::Files);
    for (const QString &file : files)
    {
        if (file.contains(QStringLiteral("litematica"), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

/** 目标目录里已有同名文件时不覆盖：自动补 (2)、(3)… */
inline QString uniquePath(const QString &dirPath, const QString &fileName)
{
    QString target = dirPath + QLatin1Char('/') + fileName;
    const QString base = QFileInfo(fileName).completeBaseName();
    const QString suffix = QFileInfo(fileName).suffix();
    const QString ext = suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
    int index = 2;
    while (QFile::exists(target))
    {
        target = QStringLiteral("%1/%2 (%3)%4").arg(dirPath, base).arg(index).arg(ext);
        ++index;
    }
    return target;
}

} // namespace SchematicDownloadTarget
