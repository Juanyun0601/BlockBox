/**
 * @file   BedrockContentInstaller.cpp
 * @brief  基岩版附加包安装器实现
 * @author BlockBox Team
 */

#include "BedrockContentInstaller.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

QString tempRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QStringLiteral("/blockbox_bedrock_packs");
}

// ── zip 解压（依次尝试 PowerShell Expand-Archive / unzip / 7z） ──────────────
// 使用 startDetached 异步执行，避免阻塞 UI 线程
bool extractZip(const QString &zipFile, const QString &destDir, QString *err)
{
    if (!QDir().mkpath(destDir)) {
        if (err) *err = QStringLiteral("无法创建解压目录: %1").arg(destDir);
        return false;
    }
    const QString src = QDir::toNativeSeparators(zipFile);
    const QString dst = QDir::toNativeSeparators(destDir);

#ifdef Q_OS_WIN
    // PowerShell Expand-Archive（异步，不阻塞 UI）
    if (QProcess::startDetached(QStringLiteral("powershell.exe"),
            QStringList() << QStringLiteral("-NoProfile")
                          << QStringLiteral("-Command")
                          << QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(src, dst)))
        return true;
#endif

    // unzip（异步）
    if (QProcess::startDetached(QStringLiteral("unzip"),
            QStringList() << QStringLiteral("-o") << zipFile << QStringLiteral("-d") << destDir))
        return true;

    // 7z（异步）
    if (QProcess::startDetached(QStringLiteral("7z"),
            QStringList() << QStringLiteral("x") << QStringLiteral("-o") + destDir
                          << QStringLiteral("-y") << zipFile))
        return true;

    if (err) *err = QStringLiteral("解压附加包失败（已尝试 Expand-Archive / unzip / 7z）");
    return false;
}

bool copyDirRecursively(const QString &srcDir, const QString &dstDir, QString *err)
{
    QDir src(srcDir);
    if (!src.exists())
        return true;
    if (!QDir().mkpath(dstDir)) {
        if (err) *err = QStringLiteral("无法创建目标目录: %1").arg(dstDir);
        return false;
    }
    QDirIterator dirIt(srcDir, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                       QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        dirIt.next();
        const QString rel = src.relativeFilePath(dirIt.filePath());
        if (!QDir().mkpath(dstDir + QLatin1Char('/') + rel)) {
            if (err) *err = QStringLiteral("无法创建目录 %1").arg(rel);
            return false;
        }
    }
    QDirIterator fileIt(srcDir, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
    while (fileIt.hasNext()) {
        fileIt.next();
        const QString rel = src.relativeFilePath(fileIt.filePath());
        if (!QFile::copy(fileIt.filePath(), dstDir + QLatin1Char('/') + rel)) {
            if (err) *err = QStringLiteral("无法复制文件 %1").arg(rel);
            return false;
        }
    }
    return true;
}

QString sanitizeFolderName(const QString &name)
{
    QString clean = name.trimmed();
    QRegularExpression re(QStringLiteral("[\\\\/:*?\"<>|]"));
    clean.replace(re, QStringLiteral("_"));
    if (clean.isEmpty())
        return QStringLiteral("pack");
    return clean.left(60);
}

QJsonArray versionToArray(const QJsonValue &v)
{
    QJsonArray arr;
    if (v.isArray()) {
        arr = v.toArray();
    } else if (v.isString()) {
        const QStringList parts = v.toString().split(QLatin1Char('.'));
        for (const QString &part : parts) {
            bool ok = false;
            int n = part.toInt(&ok);
            arr.append(ok ? n : 0);
        }
    }
    return arr;
}

// ── 登记 development_*.json ─────────────────────────────────────────────────
bool registerPack(const QString &comMojangDir, const QString &packId,
                  const QJsonArray &version, bool behavior, QString *err)
{
    const QString filePath = comMojangDir
        + (behavior ? QStringLiteral("/development_behavior_packs.json")
                    : QStringLiteral("/development_resource_packs.json"));

    QJsonArray entries;
    QFile file(filePath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error == QJsonParseError::NoError && doc.isArray())
            entries = doc.array();
    }

    // 更新或追加
    bool found = false;
    for (int i = 0; i < entries.size(); ++i) {
        QJsonObject o = entries[i].toObject();
        if (o.value(QStringLiteral("pack_id")).toString() == packId) {
            o.insert(QStringLiteral("pack_id"), packId);
            o.insert(QStringLiteral("version"), version);
            entries[i] = o;
            found = true;
            break;
        }
    }
    if (!found) {
        QJsonObject o;
        o.insert(QStringLiteral("pack_id"), packId);
        o.insert(QStringLiteral("version"), version);
        entries.append(o);
    }

    if (!QDir().mkpath(comMojangDir)) {
        if (err) *err = QStringLiteral("无法创建 com.mojang 目录");
        return false;
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("无法写入登记文件 %1").arg(filePath);
        return false;
    }
    file.write(QJsonDocument(entries).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// ── 安装单个附加包（packDir 内含 manifest.json） ─────────────────────────────
bool installSinglePack(const QString &packDir, const QString &comMojangDir, QString *err)
{
    const QFileInfo manifestInfo(packDir + QStringLiteral("/manifest.json"));
    if (!manifestInfo.exists()) {
        if (err) *err = QStringLiteral("附加包缺少 manifest.json: %1").arg(packDir);
        return false;
    }

    QFile mf(manifestInfo.absoluteFilePath());
    if (!mf.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &parseError);
    mf.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (err) *err = QStringLiteral("manifest.json 解析失败: %1").arg(packDir);
        return false;
    }

    const QJsonObject manifest = doc.object();
    const QJsonObject header = manifest.value(QStringLiteral("header")).toObject();
    const QString packId = header.value(QStringLiteral("uuid")).toString();
    const QJsonArray version = versionToArray(header.value(QStringLiteral("version")));
    if (packId.isEmpty()) {
        if (err) *err = QStringLiteral("manifest.json 缺少 header.uuid: %1").arg(packDir);
        return false;
    }

    const QJsonArray modules = manifest.value(QStringLiteral("modules")).toArray();
    bool isBehavior = false;
    bool isResource = false;
    bool isSkin = false;
    bool isWorld = false;
    for (const QJsonValue &m : modules) {
        const QString type = m.toObject().value(QStringLiteral("type")).toString();
        if (type == QLatin1String("data"))         isBehavior = true;
        else if (type == QLatin1String("resources")) isResource = true;
        else if (type == QLatin1String("skin_pack")) isSkin = true;
        else if (type == QLatin1String("world_template")) isWorld = true;
    }
    // 缺省：data 与 resources 都无时按资源包处理
    if (!isBehavior && !isResource && !isSkin && !isWorld)
        isResource = true;

    if (isWorld) {
        const QString worldsDir = comMojangDir + QStringLiteral("/minecraftWorlds");
        QString worldFolder = sanitizeFolderName(header.value(QStringLiteral("name")).toString());
        worldFolder = QStringLiteral("%1_%2").arg(worldFolder, packId.left(8));
        if (!QDir().mkpath(worldsDir)) {
            if (err) *err = QStringLiteral("无法创建世界目录");
            return false;
        }
        QString target = worldsDir + QLatin1Char('/') + worldFolder;
        QDir targetDir(target);
        if (targetDir.exists())
            targetDir.removeRecursively();
        if (!copyDirRecursively(packDir, target, err))
            return false;
        return true;
    }

    if (isBehavior || isResource) {
        const bool hasBehavior = isBehavior;
        const QString subDir = hasBehavior ? QStringLiteral("behavior_packs")
                                           : QStringLiteral("resource_packs");
        const QString packsDir = comMojangDir + QLatin1Char('/') + subDir;
        if (!QDir().mkpath(packsDir)) {
            if (err) *err = QStringLiteral("无法创建 %1 目录").arg(subDir);
            return false;
        }
        // 以 uuid 作为文件夹名，保证唯一且便于游戏识别
        const QString target = packsDir + QLatin1Char('/') + packId;
        QDir targetDir(target);
        if (targetDir.exists())
            targetDir.removeRecursively();
        if (!copyDirRecursively(packDir, target, err))
            return false;
        if (!registerPack(comMojangDir, packId, version, hasBehavior, err))
            return false;
        return true;
    }

    if (isSkin) {
        const QString skinsDir = comMojangDir + QStringLiteral("/skin_packs");
        if (!QDir().mkpath(skinsDir)) {
            if (err) *err = QStringLiteral("无法创建 skin_packs 目录");
            return false;
        }
        const QString target = skinsDir + QLatin1Char('/') + packId;
        QDir targetDir(target);
        if (targetDir.exists())
            targetDir.removeRecursively();
        if (!copyDirRecursively(packDir, target, err))
            return false;
        return true;
    }

    if (err) *err = QStringLiteral("未识别的附加包类型");
    return false;
}

} // namespace

namespace BedrockContentInstaller {

PackKind classifyFile(const QString &fileName)
{
    const QString lower = fileName.toLower();
    if (lower.endsWith(QStringLiteral(".mcworld")))
        return PackKind::World;
    return PackKind::Unknown;
}

bool installPack(const QString &packFile, const QString &comMojangDir, QString *errorMessage)
{
    const QFileInfo fi(packFile);
    if (!fi.exists()) {
        if (errorMessage) *errorMessage = QStringLiteral("附加包文件不存在: %1").arg(packFile);
        return false;
    }

    const QString root = tempRoot();
    QDir().mkpath(root);
    QTemporaryDir tmp(QStringLiteral("%1/install_XXXXXX").arg(root));
    if (!tmp.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建临时解压目录");
        return false;
    }
    const QString extractDir = tmp.path();

    if (PackKind::World == classifyFile(packFile)) {
        // .mcworld：解压后把含 level.dat 的目录放入 minecraftWorlds
        if (!extractZip(packFile, extractDir, errorMessage))
            return false;
        QString worldSource;
        const QStringList subDirs = QDir(extractDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (QFile::exists(extractDir + QStringLiteral("/level.dat"))) {
            worldSource = extractDir;
        } else {
            for (const QString &sub : subDirs) {
                if (QFile::exists(extractDir + QLatin1Char('/') + sub + QStringLiteral("/level.dat"))) {
                    worldSource = extractDir + QLatin1Char('/') + sub;
                    break;
                }
            }
        }
        if (worldSource.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("世界文件中未找到 level.dat");
            return false;
        }
        const QString worldsDir = comMojangDir + QStringLiteral("/minecraftWorlds");
        if (!QDir().mkpath(worldsDir)) {
            if (errorMessage) *errorMessage = QStringLiteral("无法创建世界目录");
            return false;
        }
        QString folder = sanitizeFolderName(fi.completeBaseName());
        QString target = worldsDir + QLatin1Char('/') + folder;
        int suffix = 1;
        while (QDir(target).exists()) {
            target = worldsDir + QLatin1Char('/') + folder + QStringLiteral(" (%1)").arg(++suffix);
        }
        if (!copyDirRecursively(worldSource, target, errorMessage))
            return false;
        return true;
    }

    // .mcpack / .mcaddon / .zip
    if (!extractZip(packFile, extractDir, errorMessage))
        return false;

    // .mcaddon 常含多个附加包子目录；.mcpack 的 manifest.json 在解压根
    const bool hasRootManifest = QFile::exists(extractDir + QStringLiteral("/manifest.json"));

    QStringList packDirs;
    if (hasRootManifest) {
        packDirs << extractDir;
    } else {
        const QFileInfoList entries = QDir(extractDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &e : entries) {
            if (e.isDir() && QFile::exists(e.absoluteFilePath() + QStringLiteral("/manifest.json")))
                packDirs << e.absoluteFilePath();
        }
        // 若根下仍是 .mcpack 文件（嵌套压缩包），逐个解压
        const QStringList files = QDir(extractDir).entryList(QStringList()
            << QStringLiteral("*.mcpack") << QStringLiteral("*.mcaddon")
            << QStringLiteral("*.zip") << QStringLiteral("*.mcworld"),
            QDir::Files);
        for (const QString &f : files) {
            const QString inner = extractDir + QStringLiteral("/packs_") + f;
            if (extractZip(extractDir + QLatin1Char('/') + f, inner, nullptr)) {
                if (QFile::exists(inner + QStringLiteral("/manifest.json")))
                    packDirs << inner;
                else {
                    const QStringList innerDirs =
                        QDir(inner).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    for (const QString &sd : innerDirs) {
                        if (QFile::exists(inner + QLatin1Char('/') + sd + QStringLiteral("/manifest.json")))
                            packDirs << inner + QLatin1Char('/') + sd;
                    }
                }
            }
        }
    }

    if (packDirs.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("未在附加包中找到 manifest.json");
        return false;
    }

    int installed = 0;
    for (const QString &dir : packDirs) {
        QString err;
        if (installSinglePack(dir, comMojangDir, &err)) {
            ++installed;
        } else if (errorMessage) {
            *errorMessage = err;
        }
    }
    if (installed == 0) {
        if (errorMessage && errorMessage->isEmpty())
            *errorMessage = QStringLiteral("附加包安装失败");
        return false;
    }
    return true;
}

} // namespace BedrockContentInstaller
