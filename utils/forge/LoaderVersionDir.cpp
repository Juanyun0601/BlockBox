/**
 * @file   LoaderVersionDir.cpp
 * @brief  加载器版本目录整理（Forge / NeoForge 共用）
 * @author BlockBox Team
 * @date   2026-10-25
 */

#include "LoaderVersionDir.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>

namespace forge {

QString resolveMinecraftRoot(const QString &instancePath)
{
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(instancePath));
    if (path.isEmpty())
        return path;

    const QFileInfo info(path);
    // 版本目录：上一级就是 versions
    if (info.absoluteDir().dirName().compare(QStringLiteral("versions"), Qt::CaseInsensitive) == 0)
        return QDir::cleanPath(info.absolutePath() + QStringLiteral("/.."));
    // 传进来的就是 versions 目录
    if (info.fileName().compare(QStringLiteral("versions"), Qt::CaseInsensitive) == 0)
        return info.absolutePath();
    // 否则认为已经是游戏根目录
    return path;
}

QJsonObject readVersionJsonFromDir(const QString &versionDir)
{
    const QString dirName = QFileInfo(versionDir).fileName();
    const QStringList candidates{versionDir + "/" + dirName + ".json",
                                 versionDir + "/version.json"};
    for (const QString &path : candidates)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject())
            return doc.object();
    }
    return QJsonObject();
}

namespace {

// 把 src 目录整体并入 dst：dst 不存在时直接改名；已存在时逐文件补齐（跳过同名文件）。
// 全部失败返回 false（目录留在原处，不影响安装成败）
bool migrateDirInto(const QString &src, const QString &dst)
{
    if (!QDir(src).exists())
        return true;

    if (!QDir(dst).exists())
        return QDir().rename(src, dst);

    QDirIterator it(src, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString filePath = it.next();
        const QString rel = QDir(src).relativeFilePath(filePath);
        const QString target = dst + "/" + rel;
        if (QFile::exists(target))
            continue;
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::rename(filePath, target))
            return false;
    }
    // 清掉搬空后残留的子目录；rename 失败（非空/被占用）说明还有文件没搬走
    return QDir(src).removeRecursively();
}

} // namespace

void consolidateLoaderVersionDir(const QString &gameRoot, const QString &mcVersion,
                                 const QString &versionId, bool copyVanillaJar)
{
    // 把原版基准目录合进加载器版本目录，让实例自包含、versions/ 下只留一个文件夹：
    // 1) 原版 JSON 按 mergeInheritsFromJson 的同套规则并进加载器 JSON，去掉 inheritsFrom；
    // 2) 需要时补拷一份 <mc>.jar（老安装器不往实例目录放客户端 jar 的情况）；
    // 3) 版本隔离模式下原版目录里的 libraries/assets 一并搬进实例目录；
    // 4) 原版目录仅在「没有其他实例仍在继承」时回收。
    // 任一前置条件不满足就保持原状（两份目录照旧能跑），不影响安装成败。
    const QString versionsDirPath = gameRoot + "/versions";
    const QString childDir = versionsDirPath + "/" + versionId;
    const QString parentDir = versionsDirPath + "/" + mcVersion;

    const QJsonObject child = readVersionJsonFromDir(childDir);
    if (child.isEmpty() || child.value("inheritsFrom").toString() != mcVersion)
    {
        qWarning() << "[LoaderVersionDir] 跳过实例目录合并: 版本 JSON 缺失或未继承"
                   << mcVersion << "(versionId:" << versionId << ")";
        return; // 没有 inheritsFrom：实例本就不依赖原版目录，无需合并
    }
    const QJsonObject parent = readVersionJsonFromDir(parentDir);
    if (parent.isEmpty())
    {
        qWarning() << "[LoaderVersionDir] 跳过实例目录合并: 原版 JSON 缺失" << parentDir;
        return; // 原版 JSON 缺失：保留两份，实例还能靠原版目录启动
    }

    QJsonObject merged = child;

    // 老安装器路径没有客户端 jar：去掉继承后必须有本体才能启动
    if (copyVanillaJar && !QFile::exists(childDir + "/" + mcVersion + ".jar"))
    {
        const QString parentJar = parentDir + "/" + mcVersion + ".jar";
        if (QFile::exists(parentJar))
            QFile::copy(parentJar, childDir + "/" + mcVersion + ".jar");
    }

    // libraries：原版在前、加载器在后（与启动时 mergeInheritsFromJson 的顺序一致）
    {
        QJsonArray libraries = parent.value("libraries").toArray();
        for (const QJsonValue &lib : child.value("libraries").toArray())
            libraries.append(lib);
        merged["libraries"] = libraries;
    }
    // arguments：子优先，缺失的顶层键从原版补
    {
        const QJsonObject parentArgs = parent.value("arguments").toObject();
        QJsonObject childArgs = merged.value("arguments").toObject();
        if (!parentArgs.isEmpty())
        {
            if (!childArgs.contains("jvm") && parentArgs.contains("jvm"))
                childArgs["jvm"] = parentArgs["jvm"];
            if (!childArgs.contains("game") && parentArgs.contains("game"))
                childArgs["game"] = parentArgs["game"];
            merged["arguments"] = childArgs;
        }
    }
    for (const char *key : {"assetIndex", "assets", "mainClass", "logging",
                            "minecraftArguments", "javaVersion", "downloads"})
    {
        const QLatin1String k(key);
        if (!merged.contains(k) && parent.contains(k))
            merged[k] = parent.value(k);
    }
    // 自包含标识：clientVersion 供实例列表显示原版版本号；
    // jar 指向实例目录里的 <mc>.jar（legacy/老安装器路径的 jar 就叫这个名）
    merged["clientVersion"] = mcVersion;
    if (QFile::exists(childDir + "/" + mcVersion + ".jar"))
        merged["jar"] = mcVersion;
    merged.remove("inheritsFrom");

    QFile out(childDir + "/" + versionId + ".json");
    if (!out.open(QIODevice::WriteOnly))
    {
        qWarning() << "[LoaderVersionDir] 跳过实例目录合并: 无法写入合并 JSON" << versionId;
        return;
    }
    out.write(QJsonDocument(merged).toJson());
    out.close();

    // 其他实例仍在继承这个原版 → 原版目录保留
    const QDir versionsRoot(versionsDirPath);
    for (const QFileInfo &sub : versionsRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (sub.fileName() == versionId || sub.fileName() == mcVersion)
            continue;
        if (readVersionJsonFromDir(sub.absoluteFilePath())
                .value("inheritsFrom")
                .toString() == mcVersion)
        {
            qWarning() << "[LoaderVersionDir] 保留原版目录: 实例" << sub.fileName()
                       << "仍在继承" << mcVersion;
            return;
        }
    }

    // 版本隔离模式下，原版的 libraries/assets 落在原版目录里；
    // 搬进加载器实例目录（启动器隔离查找顺序：实例目录 → 共享目录），实例保持自包含
    for (const char *dirName : {"libraries", "assets"})
    {
        const QLatin1String d(dirName);
        if (QDir(parentDir + "/" + d).exists())
        {
            if (!migrateDirInto(parentDir + "/" + d, childDir + "/" + d))
            {
                qWarning() << "[LoaderVersionDir] 保留原版目录: 隔离" << d << "目录迁移失败";
                return;
            }
        }
    }

    // 原版目录里出现非标准文件（版本隔离产生的存档/设置/mod 等用户数据）→ 保留。
    // 日志配置文件（logging.client 指定的 client-*.xml）是下载器写进来的，
    // 启动器每次启动会自写安全版 log4j2.xml，不需要保留
    QSet<QString> allowed{mcVersion + ".jar", mcVersion + ".json", QStringLiteral("version.json"),
                          mcVersion + "-natives", QStringLiteral(".merge-tmp.jar")};
    const QString logConfigId = parent.value("logging").toObject()
                                    .value("client").toObject()
                                    .value("file").toObject().value("id").toString();
    if (!logConfigId.isEmpty())
        allowed.insert(logConfigId);
    for (const QFileInfo &entry : QDir(parentDir).entryInfoList(
             QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
    {
        if (!allowed.contains(entry.fileName()))
        {
            qWarning() << "[LoaderVersionDir] 保留原版目录: 含非标准文件" << entry.fileName();
            return;
        }
    }
    QDir(parentDir).removeRecursively();
}

} // namespace forge
