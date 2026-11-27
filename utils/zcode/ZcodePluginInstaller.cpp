/**
 * @file   ZcodePluginInstaller.cpp
 * @brief  「方块盒子桥接」插件装进 ZCode 的实现
 * @author BlockBox Team
 * @date   2026-11-26
 */
#include "utils/zcode/ZcodePluginInstaller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace {

/** 插件包内需要释放的文件，顺序即释放顺序。 */
const QStringList &payloadList()
{
    static const QStringList files = {
        QStringLiteral(".zcode-plugin/plugin.json"),
        QStringLiteral("dist/mcp/server.mjs"),
        QStringLiteral("skills/blockbox-bridge/SKILL.md"),
        QStringLiteral("README.md"),
    };
    return files;
}

/** 市场登记文件，与插件文件分开释放。 */
const QString kMarketplaceFile = QStringLiteral("marketplace.json");

/** ZCode 时间戳格式（ISO 带毫秒与 Z）。 */
QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"));
}

/** 一个随机的安装事务 id，格式与官方安装器保持一致即可。 */
QString randomTransactionId()
{
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return uuid;
}

bool readJsonObject(const QString &path, QJsonObject *object, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        *errorMessage = QStringLiteral("读不到 %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        *errorMessage = QStringLiteral("%1 不是合法的 JSON（%2）").arg(path, parseError.errorString());
        return false;
    }

    *object = document.object();
    return true;
}

bool writeJsonObject(const QString &path, const QJsonObject &object, QString *errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        *errorMessage = QStringLiteral("写不了 %1（文件被占用或没有权限）").arg(path);
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        *errorMessage = QStringLiteral("保存 %1 失败：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

/** 删掉旧版本目录。 */
bool removeInstalledCopy(const QString &target, QString *errorMessage)
{
    const QFileInfo info(target);
    if (!info.exists())
    {
        return true;
    }
    if (info.isFile())
    {
        if (!QFile::remove(target))
        {
            *errorMessage = QStringLiteral("删不掉旧文件：%1").arg(target);
            return false;
        }
        return true;
    }
    if (!QDir(target).removeRecursively())
    {
        *errorMessage = QStringLiteral("删不掉旧插件目录：%1").arg(target);
        return false;
    }
    return true;
}

bool copyPayloadFiles(const QString &payloadDir, const QString &targetDir, QString *errorMessage)
{
    for (const QString &relative : payloadList())
    {
        const QString sourcePath = QDir(payloadDir).filePath(relative);
        const QString destinationPath = QDir(targetDir).filePath(relative);
        if (!QFileInfo::exists(sourcePath))
        {
            *errorMessage = QStringLiteral("插件目录缺少 %1").arg(relative);
            return false;
        }
        QDir().mkpath(QFileInfo(destinationPath).absolutePath());
        if (QFileInfo::exists(destinationPath) && !QFile::remove(destinationPath))
        {
            *errorMessage = QStringLiteral("覆盖不了 %1").arg(destinationPath);
            return false;
        }
        if (!QFile::copy(sourcePath, destinationPath))
        {
            *errorMessage = QStringLiteral("复制失败：%1").arg(destinationPath);
            return false;
        }
    }
    return true;
}

/** 插件缓存目录：cache/<marketId>/<pkg>/<version>。 */
QString cacheDirFor(const QString &pluginsDir)
{
    return QDir(pluginsDir).filePath(
        QStringLiteral("cache/%1/%2/%3")
            .arg(QString::fromLatin1(ZcodePluginInstaller::kMarketId),
                 QString::fromLatin1(ZcodePluginInstaller::kPackageName),
                 QString::fromLatin1(ZcodePluginInstaller::kVersion)));
}

/** 安装记录里的 id：<pkg>@<marketId>。 */
QString pluginId()
{
    return QStringLiteral("%1@%2")
        .arg(QString::fromLatin1(ZcodePluginInstaller::kPackageName),
             QString::fromLatin1(ZcodePluginInstaller::kMarketId));
}

/**
 * @brief 改市场登记文件：写成只含本插件的市场清单
 * @param pluginsDir <home>/cli/plugins 目录
 */
bool editMarketplace(const QString &pluginsDir, bool install,
                     const QString &cacheDir, QString *errorMessage)
{
    const QString marketDir = QDir(pluginsDir).filePath(
        QStringLiteral("marketplaces/%1").arg(QString::fromLatin1(ZcodePluginInstaller::kMarketId)));
    const QString marketPath = QDir(marketDir).filePath(QStringLiteral("marketplace.json"));

    if (!install)
    {
        // 卸载：市场目录整个移除（该市场只有本插件一个条目）。
        if (QFileInfo::exists(marketDir) && !QDir(marketDir).removeRecursively())
        {
            *errorMessage = QStringLiteral("删不掉市场登记目录：%1").arg(marketDir);
            return false;
        }
        return true;
    }

    // 释放打包时嵌好的市场清单，再把 cachePath 指向插件缓存目录。
    QFile source(QStringLiteral(":/resources/zcode-plugin/") + kMarketplaceFile);
    if (!source.open(QIODevice::ReadOnly))
    {
        *errorMessage = QStringLiteral("安装包缺少文件：%1").arg(kMarketplaceFile);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(source.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        *errorMessage = QStringLiteral("内置市场清单不是合法的 JSON");
        return false;
    }
    QJsonObject market = document.object();
    QJsonArray plugins = market.value(QStringLiteral("plugins")).toArray();
    for (int index = 0; index < plugins.size(); ++index)
    {
        QJsonObject entry = plugins.at(index).toObject();
        entry.insert(QStringLiteral("cachePath"),
                     QDir::fromNativeSeparators(cacheDir));
        plugins.replace(index, entry);
    }
    market.insert(QStringLiteral("plugins"), plugins);

    if (!QDir().mkpath(marketDir))
    {
        *errorMessage = QStringLiteral("建不了 %1").arg(marketDir);
        return false;
    }
    return writeJsonObject(marketPath, market, errorMessage);
}

/**
 * @brief 改 installed_plugins.json：替换或追加本插件的安装记录
 */
bool editInstalledPlugins(const QString &pluginsDir, bool install,
                          const QString &cacheDir, QString *errorMessage)
{
    const QString listPath = QDir(pluginsDir).filePath(QStringLiteral("installed_plugins.json"));
    QJsonObject root;
    if (QFileInfo::exists(listPath))
    {
        if (!readJsonObject(listPath, &root, errorMessage))
        {
            return false;
        }
    }
    else if (install)
    {
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("plugins"), QJsonArray());
    }
    else
    {
        return true; // 没有清单就视为没装过，卸载无事可做。
    }

    QJsonArray plugins = root.value(QStringLiteral("plugins")).toArray();
    QJsonArray nextPlugins;
    bool listed = false;
    for (const QJsonValue &value : plugins)
    {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString() == pluginId())
        {
            listed = true;
            if (!install)
            {
                continue; // 卸载：从清单里删掉。
            }
        }
        nextPlugins.append(value);
    }

    if (install)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), pluginId());
        entry.insert(QStringLiteral("name"), QString::fromLatin1(ZcodePluginInstaller::kPackageName));
        entry.insert(QStringLiteral("marketplace"), QString::fromLatin1(ZcodePluginInstaller::kMarketId));
        entry.insert(QStringLiteral("version"), QString::fromLatin1(ZcodePluginInstaller::kVersion));
        entry.insert(QStringLiteral("installPath"), QDir::fromNativeSeparators(cacheDir));
        const QString stamp = nowIso();
        entry.insert(QStringLiteral("installedAt"), stamp);
        entry.insert(QStringLiteral("updatedAt"), stamp);
        entry.insert(QStringLiteral("scope"), QStringLiteral("user"));
        QJsonObject source;
        source.insert(QStringLiteral("source"), QStringLiteral("filesystem"));
        source.insert(QStringLiteral("type"), QStringLiteral("directory"));
        entry.insert(QStringLiteral("source"), source);
        entry.insert(QStringLiteral("cacheTransactionId"), randomTransactionId());
        nextPlugins.append(entry);
    }

    root.insert(QStringLiteral("plugins"), nextPlugins);

    // 第一次动之前留一份原样备份，便于手动回滚。
    if (install && QFileInfo::exists(listPath))
    {
        const QString backupPath = listPath + QStringLiteral(".blockbox-backup");
        if (!QFileInfo::exists(backupPath))
        {
            QFile::copy(listPath, backupPath);
        }
    }

    return writeJsonObject(listPath, root, errorMessage);
}

} // namespace

namespace ZcodePluginInstaller {

QString defaultHome()
{
    const QString fromEnv = qEnvironmentVariable("ZCODE_CLI_HOME").trimmed();
    if (!fromEnv.isEmpty())
    {
        return QDir::fromNativeSeparators(fromEnv);
    }
    return QDir(QDir::homePath()).filePath(QStringLiteral(".zcode"));
}

QStringList payloadFiles()
{
    return payloadList();
}

Installation detectInstallation(const QString &home)
{
    const QString root = home.trimmed().isEmpty() ? defaultHome() : home;
    Installation installation;
    installation.home = root;
    installation.pluginsDir = QDir(root).filePath(QStringLiteral("cli/plugins"));
    installation.installed = isInstalled(installation.pluginsDir, &installation.version);
    return installation;
}

bool writePayload(const QString &payloadDir, QString *errorMessage, QStringList *log)
{
    if (payloadDir.trimmed().isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("插件目录为空");
        }
        return false;
    }

    QDir target(payloadDir);
    if (!target.exists() && !target.mkpath(QStringLiteral(".")))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("建不了目录：%1").arg(payloadDir);
        }
        return false;
    }

    QStringList allFiles = payloadList();
    allFiles.append(kMarketplaceFile);
    for (const QString &relative : allFiles)
    {
        QFile source(QStringLiteral(":/resources/zcode-plugin/") + relative);
        if (!source.open(QIODevice::ReadOnly))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("安装包缺少文件：%1").arg(relative);
            }
            return false;
        }

        const QString destinationPath = target.filePath(relative);
        QDir().mkpath(QFileInfo(destinationPath).absolutePath());

        QSaveFile destination(destinationPath);
        if (!destination.open(QIODevice::WriteOnly))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("写不了 %1").arg(destinationPath);
            }
            return false;
        }
        destination.write(source.readAll());
        if (!destination.commit())
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("保存 %1 失败").arg(destinationPath);
            }
            return false;
        }
        if (log)
        {
            log->append(QStringLiteral("释放 %1").arg(relative));
        }
    }
    return true;
}

bool installInto(const QString &pluginsDir, const QString &payloadDir,
                 QString *errorMessage, QStringList *log)
{
    if (pluginsDir.trimmed().isEmpty() || payloadDir.trimmed().isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("ZCode 目录或插件目录为空");
        }
        return false;
    }

    const QDir plugins(pluginsDir);
    if (!plugins.exists(QStringLiteral("cli")))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("这里不是 ZCode 数据目录：%1").arg(pluginsDir);
        }
        return false;
    }

    const QString cacheDir = cacheDirFor(pluginsDir);
    if (!removeInstalledCopy(cacheDir, errorMessage))
    {
        return false;
    }
    if (!QDir().mkpath(cacheDir))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("建不了 %1").arg(cacheDir);
        }
        return false;
    }
    if (!copyPayloadFiles(payloadDir, cacheDir, errorMessage))
    {
        return false;
    }
    if (log)
    {
        log->append(QStringLiteral("复制插件到 %1").arg(cacheDir));
    }

    if (!editMarketplace(pluginsDir, true, cacheDir, errorMessage))
    {
        return false;
    }
    if (!editInstalledPlugins(pluginsDir, true, cacheDir, errorMessage))
    {
        return false;
    }
    if (log)
    {
        log->append(QStringLiteral("更新 %1").arg(QDir(pluginsDir).filePath(QStringLiteral("installed_plugins.json"))));
    }
    return true;
}

bool uninstallFrom(const QString &pluginsDir, QString *errorMessage)
{
    const QDir plugins(pluginsDir);
    if (!plugins.exists(QStringLiteral("cli")))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("这里不是 ZCode 数据目录：%1").arg(pluginsDir);
        }
        return false;
    }

    if (!editInstalledPlugins(pluginsDir, false, QString(), errorMessage))
    {
        return false;
    }
    if (!removeInstalledCopy(cacheDirFor(pluginsDir), errorMessage))
    {
        return false;
    }
    return editMarketplace(pluginsDir, false, QString(), errorMessage);
}

bool isInstalled(const QString &pluginsDir, QString *installedVersion)
{
    if (pluginsDir.trimmed().isEmpty())
    {
        return false;
    }

    const QDir plugins(pluginsDir);
    QJsonObject root;
    QString ignored;
    if (!readJsonObject(plugins.filePath(QStringLiteral("installed_plugins.json")), &root, &ignored))
    {
        return false;
    }
    const QJsonArray pluginsList = root.value(QStringLiteral("plugins")).toArray();
    for (const QJsonValue &value : pluginsList)
    {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString() != pluginId())
        {
            continue;
        }
        const QJsonObject manifest = entry.value(QStringLiteral("manifest")).toObject();
        const QString installPath = entry.value(QStringLiteral("installPath")).toString();
        QJsonObject pluginManifest;
        if (!installPath.isEmpty()
            && readJsonObject(QDir(installPath).filePath(QStringLiteral(".zcode-plugin/plugin.json")),
                              &pluginManifest, &ignored))
        {
            if (installedVersion)
            {
                *installedVersion = pluginManifest.value(QStringLiteral("version")).toString();
            }
        }
        else if (installedVersion)
        {
            *installedVersion = entry.value(QStringLiteral("version")).toString();
        }
        return true;
    }
    return false;
}

} // namespace ZcodePluginInstaller
