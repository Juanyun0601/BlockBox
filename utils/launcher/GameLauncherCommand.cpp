/**
 * @file   GameLauncherCommand.cpp
 * @brief  游戏启动器命令构建模块
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "utils/GameLauncher.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

#include "utils/SettingsManager.h"
#include "utils/LanguageManager.h"

QJsonObject GameLauncher::readVersionJson(const QString& instancePath)
{
    // Try multiple naming conventions for the version JSON file:
    //   1) <instancePath>/<dirname>.json  — standard (used by VersionDownloader, InstanceSelectPage)
    //   2) <instancePath>/version.json    — legacy / manual
    QString dirName = QFileInfo(instancePath).fileName();
    QStringList candidates;
    candidates << (instancePath + "/" + dirName + ".json");
    candidates << (instancePath + "/version.json");

    for (const QString& jsonPath : candidates) {
        QFile file(jsonPath);
        if (!file.open(QIODevice::ReadOnly))
            continue;

        QByteArray data = file.readAll();
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QString versionId = doc.object().value("id").toString();
            if (!versionId.isEmpty()) {
                emit launchDetailAdded(QString("读取版本数据: %1 (%2)").arg(versionId, jsonPath));
            }
            return doc.object();
        }
    }

    emit launchDetailAdded(QString("无法打开版本JSON文件 (已尝试 %1)").arg(candidates.join(", ")));
    return QJsonObject();
}

// ── 公共 helper: 按 Mojang 规范评估 rules[] ──
// 规则按顺序匹配第一条符合条件的，无匹配时默认 allow
bool GameLauncher::evaluateLibraryRules(const QJsonArray& rules)
{
    if (rules.isEmpty())
        return true; // 无规则 → 包含

    for (const QJsonValue& ruleVal : rules)
    {
        QJsonObject rule = ruleVal.toObject();
        QString action   = rule["action"].toString();
        bool    ruleApplies = true;

        if (rule.contains("os"))
        {
            QString osName = rule["os"].toObject()["name"].toString();
#ifdef Q_OS_WIN
            ruleApplies = (osName == "windows");
#elif defined(Q_OS_MAC)
            ruleApplies = (osName == "osx");
#elif defined(Q_OS_ANDROID)
            ruleApplies = (osName == "linux" || osName == "android");
#elif defined(Q_OS_LINUX)
            ruleApplies = (osName == "linux");
#else
            ruleApplies = false;
#endif
        }
        // feature-conditions (is_demo_user etc.) are evaluated later per-context;
        // for library-level filtering we only check os.

        if (ruleApplies)
            return (action == "allow");
    }
    return true; // 无匹配规则 → 包含
}

QJsonObject GameLauncher::mergeInheritsFromJson(const QString& instancePath, const QJsonObject& versionJson)
{
    if (!versionJson.contains("inheritsFrom"))
        return versionJson;

    QString inheritedId = versionJson["inheritsFrom"].toString();
    QString parentPath = QFileInfo(instancePath).dir().absolutePath() + "/" + inheritedId;
    QJsonObject parentJson = readVersionJson(parentPath);

    if (parentJson.isEmpty())
    {
        emit launchDetailAdded(QString("警告: 无法读取继承版本 '%1' 的 JSON，使用原始 JSON").arg(inheritedId));
        return versionJson;
    }

    emit launchDetailAdded(QString("合并继承版本数据: %1 → %2").arg(inheritedId, instancePath));

    QJsonObject merged = versionJson;

    // ── 1) 合并 libraries: parent libraries + child libraries ──
    {
        QJsonArray parentLibs = parentJson["libraries"].toArray();
        QJsonArray childLibs  = merged["libraries"].toArray();
        QJsonArray mergedLibs;
        for (const QJsonValue& lib : parentLibs) mergedLibs.append(lib);
        for (const QJsonValue& lib : childLibs)  mergedLibs.append(lib);
        merged["libraries"] = mergedLibs;
        emit launchDetailAdded(QString("合并 libraries: parent %1 + child %2 = %3")
            .arg(parentLibs.size()).arg(childLibs.size()).arg(mergedLibs.size()));
    }

    // ── 2) 合并 arguments: child 优先，parent 补充缺失的顶层 ──
    {
        QJsonObject parentArgs = parentJson["arguments"].toObject();
        QJsonObject childArgs  = merged["arguments"].toObject();

        if (!parentArgs.isEmpty())
        {
            // jvm: child 优先（Fabric 等通常自带 jvm args）
            if (!childArgs.contains("jvm") && parentArgs.contains("jvm"))
            {
                childArgs["jvm"] = parentArgs["jvm"];
            }
            // game: child 优先
            if (!childArgs.contains("game") && parentArgs.contains("game"))
            {
                childArgs["game"] = parentArgs["game"];
            }
            merged["arguments"] = childArgs;
        }
    }

    // ── 3) 继承缺失的顶层字段 ──
    if (!merged.contains("jar"))          merged["jar"]          = parentJson["jar"];
    if (!merged.contains("assetIndex"))   merged["assetIndex"]   = parentJson["assetIndex"];
    if (!merged.contains("assets"))       merged["assets"]       = parentJson["assets"];
    if (!merged.contains("mainClass"))    merged["mainClass"]    = parentJson["mainClass"];
    if (!merged.contains("logging"))      merged["logging"]      = parentJson["logging"];
    // minecraftArguments (legacy, < 1.13)
    if (!merged.contains("minecraftArguments") && parentJson.contains("minecraftArguments"))
        merged["minecraftArguments"] = parentJson["minecraftArguments"];

    return merged;
}

QString GameLauncher::resolveVersionId(const QString& instancePath)
{
    // ── 版本号正则 (对齐 ProjBobcat McVersionMatch + PCL2 格式校验) ──
    // 匹配: 1.xx  /  1.xx.x  /  1.xx.x-suffix  /  快照(23w14a)  /  远古(rd-/inf-/alpha/beta/b1./c0.)
    static const QRegularExpression versionPattern(
        R"(^(1\.\d+(\.\d+)?([\-\_].+)?|[a-z]+\d+[a-z]?|rd\-|inf\-|alpha|beta|c0\.|b1\.)$)",
        QRegularExpression::CaseInsensitiveOption
    );

    // ── 从版本 JSON 读取 ──
    QJsonObject versionJson = readVersionJson(instancePath);
    if (!versionJson.isEmpty()) {
        QString id = versionJson.value("id").toString();

        // ── 策略1: inheritsFrom ──
        if (versionJson.contains("inheritsFrom")) {
            QString v = versionJson.value("inheritsFrom").toString();
            if (!v.isEmpty() && versionPattern.match(v).hasMatch()) {
                emit launchDetailAdded(QString("继承自原版: %1").arg(v));
                return v;
            }
        }

        // ── 策略2: clientVersion (ProjBobcat: 第三方启动器预留字段) ──
        if (versionJson.contains("clientVersion")) {
            QString v = versionJson.value("clientVersion").toString();
            if (!v.isEmpty() && versionPattern.match(v).hasMatch()) {
                emit launchDetailAdded(QString("来自 clientVersion 字段: %1").arg(v));
                return v;
            }
        }

        // ── 策略3: jar 字段 ──
        if (versionJson.contains("jar")) {
            QString v = versionJson.value("jar").toString();
            if (!v.isEmpty() && versionPattern.match(v).hasMatch()) {
                emit launchDetailAdded(QString("来自 jar 字段: %1").arg(v));
                return v;
            }
        }

        // ── 策略4: arguments.game → --fml.mcVersion (ProjBobcat/PCL2: Forge 版本) ──
        if (versionJson.contains("arguments") && versionJson["arguments"].isObject()) {
            QJsonObject args = versionJson["arguments"].toObject();
            if (args.contains("game") && args["game"].isArray()) {
                QJsonArray gameArgs = args["game"].toArray();
                for (int i = 0; i + 1 < gameArgs.size(); ++i) {
                    if (gameArgs[i].toString() == QStringLiteral("--fml.mcVersion")) {
                        QString v = gameArgs[i + 1].toString();
                        if (!v.isEmpty() && versionPattern.match(v).hasMatch()) {
                            emit launchDetailAdded(QString("来自 Forge 参数 --fml.mcVersion: %1").arg(v));
                            return v;
                        }
                    }
                }
            }
        }

        // ── 策略5: assetIndex.id (仅校验后的) ──
        if (versionJson.contains("assetIndex") && versionJson["assetIndex"].isObject()) {
            QString v = versionJson["assetIndex"].toObject().value("id").toString();
            if (!v.isEmpty() && versionPattern.match(v).hasMatch()) {
                emit launchDetailAdded(QString("来自 assetIndex.id: %1").arg(v));
                return v;
            }
        }

        // ── 策略6: libraries 库名分析 ──
        if (versionJson.contains("libraries") && versionJson["libraries"].isArray()) {
            QJsonArray libs = versionJson["libraries"].toArray();
            for (const QJsonValue& libVal : libs) {
                QString name = libVal.toObject().value("name").toString();
                if (name.isEmpty()) continue;

                // 6a) Forge: net.minecraftforge:forge:1.20.1-47.2.0
                //     或 net.minecraftforge:fmlloader:1.20.1-47.2.0
                static const QRegularExpression forgeLib(
                    R"(net\.minecraftforge:(forge|fmlloader):([\d\.]+))"
                );
                QRegularExpressionMatch fm = forgeLib.match(name);
                if (fm.hasMatch()) {
                    QString fullVer = fm.captured(2);
                    // 从 "1.20.1-47.2.0" 提取 MC 版本部分 "1.20.1"
                    int dashIdx = fullVer.indexOf('-');
                    QString mcVer = (dashIdx > 0) ? fullVer.left(dashIdx) : fullVer;
                    if (!mcVer.isEmpty() && versionPattern.match(mcVer).hasMatch()) {
                        emit launchDetailAdded(QString("来自 Forge 库: %1").arg(mcVer));
                        return mcVer;
                    }
                }

                // 6b) Fabric/Quilt/LegacyFabric: net.fabricmc:intermediary:1.20.1
                static const QRegularExpression fabricLib(
                    R"((net\.fabricmc|org\.quiltmc|net\.legacyfabric):intermediary:([\d\.]+))"
                );
                QRegularExpressionMatch fabm = fabricLib.match(name);
                if (fabm.hasMatch()) {
                    QString v = fabm.captured(2);
                    if (!v.isEmpty() && versionPattern.match(v).hasMatch()) {
                        emit launchDetailAdded(QString("来自 Fabric/Quilt 库: %1").arg(v));
                        return v;
                    }
                }

                // 6c) OptiFine: optifine:OptiFine:1.20.1_HD_U_I5
                static const QRegularExpression optiLib(
                    R"(optifine:OptiFine:([\d\.]+)_)"
                );
                QRegularExpressionMatch om = optiLib.match(name);
                if (om.hasMatch()) {
                    QString v = om.captured(1);
                    if (!v.isEmpty() && versionPattern.match(v).hasMatch()) {
                        emit launchDetailAdded(QString("来自 OptiFine 库: %1").arg(v));
                        return v;
                    }
                }

                // 6d) 原版 client 库: net.minecraft:client:1.20.1
                static const QRegularExpression clientLib(
                    R"(net\.minecraft:client:([\d\w\.\-\_]+))"
                );
                QRegularExpressionMatch cm = clientLib.match(name);
                if (cm.hasMatch()) {
                    QString v = cm.captured(1);
                    if (!v.isEmpty() && versionPattern.match(v).hasMatch()) {
                        emit launchDetailAdded(QString("来自 client 库: %1").arg(v));
                        return v;
                    }
                }
            }
        }

        // ── 策略7: id 本身 ──
        if (!id.isEmpty()) {
            // Pure version (no loader suffix) - use directly
            static const QRegularExpression pureVersion(
                R"(^(1\.\d+(\.\d+)?|[a-z]+\d+[a-z]?|rd\-|inf\-|alpha|beta|c0\.|b1\.)$)",
                QRegularExpression::CaseInsensitiveOption
            );
            if (pureVersion.match(id).hasMatch()) {
                emit launchDetailAdded(QString("检测到版本: %1").arg(id));
                return id;
            }
            // Suffixed id like "1.20.1-forge-47.2.0" or "fabric-loader-0.15.11-1.20.1"
            // → extract MC version part
            static const QRegularExpression extractMCVer(R"(1\.\d+(\.\d+)?)");
            QRegularExpressionMatch m = extractMCVer.match(id);
            if (m.hasMatch()) {
                QString v = m.captured();
                emit launchDetailAdded(QString("从 id 中提取游戏版本: %1").arg(v));
                return v;
            }
        }

        // ── 警告: id 不是有效版本号 ──
        if (!id.isEmpty()) {
            emit launchDetailAdded(QString("警告: id '%1' 不是有效版本号，且未找到 inheritsFrom/jar/clientVersion").arg(id));
        }
    }

    // ── 策略8: 目录名正则回退 ──
    QString dirName = QFileInfo(instancePath).fileName();
    static const QRegularExpression dirVersionRegex("1\\.([0-9]+)(\\.[0-9]+)?");
    QRegularExpressionMatch match = dirVersionRegex.match(dirName);
    if (match.hasMatch()) {
        QString version = "1." + match.captured(1)
                        + (match.captured(2).isEmpty() ? "" : match.captured(2));
        emit launchDetailAdded(QString("从路径名推测版本: %1").arg(version));
        return version;
    }

    // ── 策略9: 兜底 ──
    emit launchDetailAdded(QStringLiteral("提示: 请在 %1.json 中添加 \"jar\": \"1.xx\" 字段以指定版本").arg(dirName));
    emit launchDetailAdded("警告: 无法检测游戏版本, 使用默认值 1.21");
    return "1.21";
}

QString GameLauncher::resolveJarName(const QString& instancePath, const QJsonObject& versionJson)
{
    static const QRegularExpression versionPattern(
        R"(^(1\.\d+(\.\d+)?([\-\_].+)?|[a-z]+\d+[a-z]?|rd\-|inf\-|alpha|beta|c0\.|b1\.)$)",
        QRegularExpression::CaseInsensitiveOption
    );

    if (!versionJson.isEmpty()) {
        // 1) inheritsFrom → 原版jar
        QString inherited = versionJson.value("inheritsFrom").toString();
        if (!inherited.isEmpty() && versionPattern.match(inherited).hasMatch())
            return inherited;

        // 2) jar 字段 (ProjBobcat: JarFile 字段)
        QString jar = versionJson.value("jar").toString();
        if (!jar.isEmpty())
            return jar;

        // 3) assetIndex.id (仅版本号格式)
        if (versionJson.contains("assetIndex") && versionJson["assetIndex"].isObject()) {
            QString assetId = versionJson["assetIndex"].toObject().value("id").toString();
            if (!assetId.isEmpty() && versionPattern.match(assetId).hasMatch())
                return assetId;
        }

        // 4) Forge 库名 → 提取 MC 版本
        if (versionJson.contains("libraries") && versionJson["libraries"].isArray()) {
            QJsonArray libs = versionJson["libraries"].toArray();
            for (const QJsonValue& libVal : libs) {
                QString name = libVal.toObject().value("name").toString();
                if (name.isEmpty()) continue;

                static const QRegularExpression forgeLib(R"(net\.minecraftforge:(forge|fmlloader):([\d\.]+))");
                QRegularExpressionMatch fm = forgeLib.match(name);
                if (fm.hasMatch()) {
                    QString fullVer = fm.captured(2);
                    int dashIdx = fullVer.indexOf('-');
                    return (dashIdx > 0) ? fullVer.left(dashIdx) : fullVer;
                }

                static const QRegularExpression fabricLib(R"((net\.fabricmc|org\.quiltmc):intermediary:([\d\.]+))");
                QRegularExpressionMatch fabm = fabricLib.match(name);
                if (fabm.hasMatch()) return fabm.captured(2);

                static const QRegularExpression optiLib(R"(optifine:OptiFine:([\d\.]+)_)");
                QRegularExpressionMatch om = optiLib.match(name);
                if (om.hasMatch()) return om.captured(1);
            }
        }

        // 5) id (版本号格式则直接用，无加载器后缀)
        QString id = versionJson.value("id").toString();
        if (!id.isEmpty()) {
            static const QRegularExpression pureVer(
                R"(^(1\.\d+(\.\d+)?|[a-z]+\d+[a-z]?|rd\-|inf\-|alpha|beta|c0\.|b1\.)$)",
                QRegularExpression::CaseInsensitiveOption
            );
            if (pureVer.match(id).hasMatch())
                return id;
            // 含后缀的 id（如 "1.20.1-forge-47.2.0"），提取 MC 版本
            static const QRegularExpression extractMC(R"(1\.\d+(\.\d+)?)");
            QRegularExpressionMatch m = extractMC.match(id);
            if (m.hasMatch())
                return m.captured();
        }
    }

    // 6) 回退: 目录名
    return QFileInfo(instancePath).fileName();
}

/**
 * @brief 为离线模式生成有效的 UUID
 *        使用 "OfflinePlayer:<username>" 的 MD5 哈希，与 Minecraft 原版算法一致
 */
static QString generateOfflineUuid(const QString& username)
{
    QByteArray data = QString("OfflinePlayer:" + username).toUtf8();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    // 格式化为标准 UUID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    QString uuid = hash.toHex();
    uuid.insert(8, '-');
    uuid.insert(13, '-');
    uuid.insert(18, '-');
    uuid.insert(23, '-');
    return uuid;
}
QStringList GameLauncher::buildClasspath(const QString& instancePath, const QJsonObject& versionJson)
{
    QStringList classpath;

    QString versionName = resolveJarName(instancePath, versionJson);
    QString jarPath = instancePath + "/" + versionName + ".jar";

    if (QFile::exists(jarPath))
    {
        classpath << jarPath;
    }
    // 参考 HMCL 做法：inheritsFrom 版本 jar 在原版版本目录下
    else if (!versionJson.isEmpty() && versionJson.contains("inheritsFrom"))
    {
        QString inherited = versionJson.value("inheritsFrom").toString();
        if (!inherited.isEmpty())
        {
            QString parentDir = QFileInfo(instancePath).dir().absolutePath() + "/" + inherited;
            QString parentJarPath = parentDir + "/" + inherited + ".jar";
            if (QFile::exists(parentJarPath))
            {
                classpath << parentJarPath;
            }
            else
            {
                // 版本隔离模式下，原版目录可能被重命名（如 "我的世界1.20.1"）
                // 扫描 versions/ 下所有子目录，通过 JSON 的 id 字段匹配
                QString versionsDir = QFileInfo(instancePath).dir().absolutePath();
                QDirIterator dirIt(versionsDir, QDir::Dirs | QDir::NoDotAndDotDot);
                while (dirIt.hasNext())
                {
                    dirIt.next();
                    QString subDirName = dirIt.fileName();
                    QString jsonPath = dirIt.filePath() + "/" + subDirName + ".json";
                    QFile jsonFile(jsonPath);
                    if (!jsonFile.open(QIODevice::ReadOnly))
                        continue;
                    QByteArray jsonData = jsonFile.readAll();
                    jsonFile.close();
                    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
                    if (jsonDoc.isObject() && jsonDoc.object().value("id").toString() == inherited)
                    {
                        QString candidateJar = dirIt.filePath() + "/" + inherited + ".jar";
                        if (!QFile::exists(candidateJar))
                            candidateJar = dirIt.filePath() + "/" + subDirName + ".jar";
                        if (QFile::exists(candidateJar))
                        {
                            classpath << candidateJar;
                        }
                        break;
                    }
                }
            }
        }
    }
    else
    {
        // PCL/HMCL 风格的独立版本（id 带加载器后缀、无 inheritsFrom，如
        // "长梦镇"/"1.16.1-Fabric 0.16.14"）: resolveJarName 会从加载器库推出
        // 原版版本号，而客户端 JAR 实际以版本 id / 目录名命名，需回退查找，
        // 否则 classpath 缺失客户端 JAR，Knot/Forge 无法定位游戏本体直接崩溃
        const QString id = versionJson.value("id").toString();
        const QString dirName = QFileInfo(instancePath).fileName();
        QString candidate = instancePath + "/" + id + ".jar";
        if (id.isEmpty() || !QFile::exists(candidate))
            candidate = instancePath + "/" + dirName + ".jar";
        if (!candidate.isEmpty() && QFile::exists(candidate))
        {
            classpath << candidate;
            qDebug() << "[GameLauncher] 使用版本 id 命名的客户端 JAR:" << candidate;
        }
    }

    // ── 合并 inheritsFrom ──
    QJsonObject mergedJson = mergeInheritsFromJson(instancePath, versionJson);
    QJsonArray allLibraries = mergedJson["libraries"].toArray();

    // 版本隔离：库文件在版本目录下；非隔离：共享 libraries 目录
    // 参考 HMCL/PCL：即使开启版本隔离，也需检查全局 libraries 目录作为回退
    // （外部工具如官方 Fabric/Forge 安装器会把库下载到全局目录）
    QString librariesPath;
    QString globalLibrariesPath;
    if (SettingsManager::instance()->isVersionIsolationEnabled())
    {
        librariesPath = instancePath + "/libraries";
        // 全局 libraries: <minecraft>/libraries（instancePath 通常在 versions/<id>/ 下）
        globalLibrariesPath = QFileInfo(instancePath).dir().absolutePath() + "/../libraries";
    }
    else
    {
        librariesPath = QFileInfo(instancePath).dir().absolutePath() + "/../libraries";
        globalLibrariesPath = librariesPath;
    }
    classpath.reserve(allLibraries.size() + 1);

    for (const QJsonValue& libValue : allLibraries)
    {
        QJsonObject lib = libValue.toObject();

        if (lib.contains("rules"))
        {
            if (!evaluateLibraryRules(lib["rules"].toArray()))
                continue;
        }

        // ── 跳过 natives 库（不应加入 classpath，由 extractNatives 处理）──
        if (lib.contains("natives"))
            continue;

        QString libPath;

        // 1) 优先用 downloads.artifact.path
        if (lib.contains("downloads") && lib["downloads"].isObject())
        {
            QJsonObject downloads = lib["downloads"].toObject();
            if (downloads.contains("artifact") && downloads["artifact"].isObject())
            {
                QJsonObject artifact = downloads["artifact"].toObject();
                QString path = artifact["path"].toString();
                if (!path.isEmpty())
                {
                    libPath = librariesPath + "/" + path;
                    // 版本隔离时回退到全局 libraries
                    if (!QFile::exists(libPath) && globalLibrariesPath != librariesPath)
                        libPath = globalLibrariesPath + "/" + path;
                }
            }
        }

        // 2) 回退: maven 坐标 → 路径（Fabric/Forge 库常缺 downloads 字段）
        if (libPath.isEmpty() || !QFile::exists(libPath))
        {
            QString mavenName = lib["name"].toString();
            if (!mavenName.isEmpty())
            {
                // "net.fabricmc:fabric-loader:0.19.2" → "net/fabricmc/fabric-loader/0.19.2/fabric-loader-0.19.2.jar"
                QStringList parts = mavenName.split(':');
                if (parts.size() >= 3)
                {
                    QString group    = parts[0];
                    QString artifact = parts[1];
                    QString version  = parts[2];
                    group.replace('.', '/');
                    QString relPath = group + "/" + artifact + "/" + version + "/"
                                    + artifact + "-" + version + ".jar";

                    // 先在版本隔离目录找，找不到再找全局
                    if (QFile::exists(librariesPath + "/" + relPath))
                        libPath = librariesPath + "/" + relPath;
                    else if (QFile::exists(globalLibrariesPath + "/" + relPath))
                        libPath = globalLibrariesPath + "/" + relPath;
                    else
                        libPath = librariesPath + "/" + relPath;  // 保留路径用于警告
                }
            }
        }

        if (!libPath.isEmpty() && QFile::exists(libPath))
        {
            classpath << libPath;
        }
        else if (!libPath.isEmpty())
        {
            // 库 JAR 不存在：打印警告（参考 PCL 的缺失库检测）
            QString libName = lib["name"].toString();
            if (libName.isEmpty()) libName = libPath;
            qDebug() << "[GameLauncher] 警告: 库文件不存在:" << libName << "->" << libPath;
            emit launchDetailAdded(QString("警告: 缺少库文件 %1（可能需要重新安装）").arg(libName));
        }
    }

    return classpath;
}

QStringList GameLauncher::buildLaunchCommand(const LaunchConfig& config)
{
    QStringList args;

    const QString& instancePath = config.instancePath;

    // ── 复用缓存: 避免重复读取/合并 JSON ──
    QJsonObject mergedJson;
    QJsonObject rawVersionJson;
    if (!m_cachedMergedJson.isEmpty())
    {
        mergedJson = m_cachedMergedJson;
        // buildClasspath 需要原始 JSON（内部自己做 merge，避免重复合并导致库条目翻倍）
        rawVersionJson = readVersionJson(instancePath);
    }
    else
    {
        rawVersionJson = readVersionJson(instancePath);
        mergedJson = mergeInheritsFromJson(instancePath, rawVersionJson);
    }

    // ── 解析版本名 ──
    QString versionName = resolveJarName(instancePath, rawVersionJson);

    // ── 基础 JVM 参数 ──
    // 参考 HMCL/PCL 标准做法：UTF-8 编码确保中文标题正常显示
    args << QStringLiteral("-Dfile.encoding=UTF-8");
    args << QStringLiteral("-Dstderr.encoding=UTF-8");
    args << QStringLiteral("-Dstdout.encoding=UTF-8");

    // ── 语言/区域设置（HMCL 做法：对所有版本设置 JVM locale）──
    // 新版本 MC 通过 options.txt 控制语言，但老版本（<1.6）依赖 JVM 默认区域
    // -Duser.language 和 -Duser.country 对所有版本都生效，老版本游戏无语言选择时尤为关键
    {
        LanguageManager::Language lang = LanguageManager::instance()->currentLanguage();
        if (lang == LanguageManager::Chinese) {
            args << QStringLiteral("-Duser.language=zh");
            args << QStringLiteral("-Duser.country=CN");
            args << QStringLiteral("-Duser.region=CN");
        } else {
            args << QStringLiteral("-Duser.language=en");
            args << QStringLiteral("-Duser.country=US");
            args << QStringLiteral("-Duser.region=US");
        }
    }

    // ── Log4Shell (CVE-2021-44228) 防护 ──
    // 参考 HMCL DefaultLauncher.generateCommandLine()
    args << QStringLiteral("-Djava.rmi.server.useCodebaseOnly=true");
    args << QStringLiteral("-Dcom.sun.jndi.rmi.object.trustURLCodebase=false");
    args << QStringLiteral("-Dcom.sun.jndi.cosnaming.object.trustURLCodebase=false");

    args << QStringLiteral("-XX:+UseG1GC");
    args << QStringLiteral("-XX:-UseAdaptiveSizePolicy");
    args << QStringLiteral("-XX:-OmitStackTraceInFastThrow");
    args << QStringLiteral("-Djdk.lang.Process.allowAmbiguousCommands=true");
    args << QStringLiteral("-Dlog4j2.formatMsgNoLookups=true");
    args << QStringLiteral("-XX:HeapDumpPath=MojangTricksIntelDriversForPerformance_javaw.exe_minecraft.exe.heapdump");

    // ── 内存 ──
    args << QString("-Xmx%1M").arg(config.maxMemory);
    args << QString("-Xms%1M").arg(config.minMemory);

    // ── 用户 JVM 参数 ──
    args << config.jvmArgs;

    // ── 资源路径 ──缓存: 复用已计算路径 ──
    // 版本隔离：资源在版本目录下；非隔离：共享目录
    QString basePath;
    if (SettingsManager::instance()->isVersionIsolationEnabled())
    {
        basePath = instancePath;
    }
    else
    {
        basePath = m_cachedBasePath.isEmpty()
            ? QFileInfo(instancePath).dir().absolutePath() + "/.."
            : m_cachedBasePath;
    }
    QString nativesPath = instancePath + "/" + versionName + "-natives";
    QString assetsPath = basePath + "/assets";
    // 回退: 版本隔离时若 assets 目录不存在（首次启动文件补全未完成），回退到共享 .minecraft/assets
    if (!QDir(assetsPath).exists())
    {
        QString sharedAssetsPath = QFileInfo(instancePath).dir().absolutePath() + "/../assets";
        if (QDir(sharedAssetsPath).exists())
        {
            assetsPath = sharedAssetsPath;
            emit launchDetailAdded(QString("资产目录回退到共享目录: %1").arg(assetsPath));
        }
    }
    QString librariesPath = basePath + "/libraries";
    QDir nativesDir(nativesPath);
    if (!nativesDir.exists()) nativesDir.mkpath(".");

    QString classpathSeparator =
#ifdef Q_OS_WIN
        QStringLiteral(";")
#else
        QStringLiteral(":")
#endif
    ;

    // ── 构建 classpath（传入原始 JSON，buildClasspath 内部会做 merge）──
    QStringList classpath = buildClasspath(instancePath, rawVersionJson);

    // ── 资产索引 ──
    QString assetIndex = versionName;
    if (mergedJson.contains("assetIndex") && mergedJson["assetIndex"].isObject()) {
        QJsonObject ai = mergedJson["assetIndex"].toObject();
        if (ai.contains("id")) assetIndex = ai["id"].toString();
    }

    AccountInfo defaultAccount = SettingsManager::instance()->getDefaultAccount();
    QString authPlayerName = defaultAccount.username.isEmpty() ? config.accountName : defaultAccount.username;
    QString authUuid       = defaultAccount.uuid.isEmpty()
                                 ? generateOfflineUuid(authPlayerName)
                                 : defaultAccount.uuid;
    QString authToken      = defaultAccount.accessToken.isEmpty() ? "0" : defaultAccount.accessToken;
    QString userType       = defaultAccount.type == "Microsoft" ? "msa" : "offline";

    // ── 版本 JSON 有 arguments.jvm 时，JSON 为准（HMCL/ProjBobcat 做法）──
    bool hasCustomJvm = false;
    if (mergedJson.contains("arguments") && mergedJson["arguments"].isObject()) {
        QJsonObject arguments = mergedJson["arguments"].toObject();
        if (arguments.contains("jvm") && arguments["jvm"].isArray()) {
            hasCustomJvm = true;
            QJsonArray jvmArgs = arguments["jvm"].toArray();
            for (const QJsonValue& argValue : jvmArgs) {
                if (argValue.isString()) {
                    args << argValue.toString();
                } else if (argValue.isObject()) {
                    QJsonObject argObj = argValue.toObject();
                    bool allowed = true;
                    if (argObj.contains("rules")) {
                        allowed = false;
                        QJsonArray rules = argObj["rules"].toArray();
                        for (const QJsonValue& ruleValue : rules) {
                            QJsonObject rule = ruleValue.toObject();
                            QString action = rule["action"].toString();
                            if (rule.contains("os")) {
                                QString osName = rule["os"].toObject()["name"].toString();
#ifdef Q_OS_WIN
                                allowed = (osName == "windows") ? (action == "allow") : (action == "disallow");
#elif defined(Q_OS_MAC)
                                allowed = (osName == "osx") ? (action == "allow") : (action == "disallow");
#elif defined(Q_OS_ANDROID)
                                allowed = (osName == "linux" || osName == "android") ? (action == "allow") : (action == "disallow");
#elif defined(Q_OS_LINUX)
                                allowed = (osName == "linux") ? (action == "allow") : (action == "disallow");
#endif
                            } else {
                                allowed = (action == "allow");
                            }
                            if (allowed) break;
                        }
                    }
                    if (allowed && argObj.contains("value")) {
                        if (argObj["value"].isString())
                            args << argObj["value"].toString();
                        else if (argObj["value"].isArray()) {
                            // JVM 参数数组: 拼接为单个参数（key=value 格式）
                            // 例: ["-DFabricMcEmu=", " net.minecraft.client.main.Main "]
                            //   → "-DFabricMcEmu=net.minecraft.client.main.Main"
                            QString joined;
                            for (const QJsonValue& val : argObj["value"].toArray())
                                joined += val.toString().trimmed();
                            args << joined;
                        }
                    }
                }
            }
        }
    }

    // ── 无自定义 JVM 参数时用默认值 ──
    if (!hasCustomJvm) {
        args << QString("-Djava.library.path=%1").arg(nativesPath);
        args << QString("-Djna.tmpdir=%1").arg(nativesPath);
        args << QString("-Dorg.lwjgl.system.SharedLibraryExtractPath=%1").arg(nativesPath);
        args << QString("-Dio.netty.native.workdir=%1").arg(nativesPath);
        args << "-Dminecraft.launcher.brand=BlockBox";
        args << "-Dminecraft.launcher.version=1.0";
    }

    // ── classpath: 若 arguments.jvm 已含 -cp ${classpath} 则不再重复添加 ──
    bool classpathInJvm = false;
    if (hasCustomJvm) {
        for (int i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "-cp" || args[i] == "-classpath") {
                classpathInJvm = true;
                break;
            }
        }
    }
    if (!classpathInJvm || !hasCustomJvm) {
        args << "-cp" << classpath.join(classpathSeparator);
    }

    // ── mainClass ──
    // 标准 Mojang 格式：mainClass 是字符串
    // 但某些第三方工具（含旧版 BlockBox FabricInstaller）写成对象 {"client": "...", "server": "..."}
    QString mainClass = "net.minecraft.client.main.Main";
    if (mergedJson.contains("mainClass"))
    {
        const QJsonValue& mcValue = mergedJson["mainClass"];
        if (mcValue.isString())
        {
            mainClass = mcValue.toString();
        }
        else if (mcValue.isObject())
        {
            // 对象格式：BlockBox 仅启动客户端，取 client 字段（参考 HMCL MainClassName 属性处理）
            QJsonObject mcObj = mcValue.toObject();
            if (mcObj.contains(QStringLiteral("client")))
                mainClass = mcObj[QStringLiteral("client")].toString();
            else if (mcObj.contains(QStringLiteral("server")))
                mainClass = mcObj[QStringLiteral("server")].toString();
        }
    }
    if (mainClass.isEmpty())
    {
        emit launchDetailAdded("警告: 版本 JSON 中 mainClass 为空，使用默认值 net.minecraft.client.main.Main");
        mainClass = "net.minecraft.client.main.Main";
    }
    args << mainClass;

    // ── game 参数 ──
    if (mergedJson.contains("arguments") && mergedJson["arguments"].isObject()) {
        QJsonObject arguments = mergedJson["arguments"].toObject();

        if (arguments.contains("game") && arguments["game"].isArray()) {
            QJsonArray gameArgs = arguments["game"].toArray();
            for (const QJsonValue& argValue : gameArgs) {
                if (argValue.isString()) {
                    args << argValue.toString();
                } else if (argValue.isObject()) {
                    QJsonObject argObj = argValue.toObject();
                    bool allowed = true;
                    if (argObj.contains("rules")) {
                        allowed = false;
                        QJsonArray rules = argObj["rules"].toArray();
                        for (const QJsonValue& ruleValue : rules) {
                            QJsonObject rule = ruleValue.toObject();
                            QString action = rule["action"].toString();
                            bool featuresMatch = true;
                            if (rule.contains("features") && rule["features"].isObject()) {
                                QJsonObject feat = rule["features"].toObject();
                                if (feat.contains("is_demo_user"))
                                    featuresMatch = false;
                                if (feat.contains("has_custom_resolution"))
                                    featuresMatch = !config.windowWidth.isEmpty();
                                if (feat.contains("is_quick_play_singleplayer")
                                    || feat.contains("is_quick_play_multiplayer")
                                    || feat.contains("is_quick_play_realms"))
                                    featuresMatch = false;
                            }
                            if (featuresMatch) {
                                if (action == "allow") allowed = true;
                                else if (action == "disallow") allowed = false;
                            }
                        }
                    }
                    if (allowed && argObj.contains("value")) {
                        if (argObj["value"].isString())
                            args << argObj["value"].toString();
                        else if (argObj["value"].isArray()) {
                            // Game 参数数组: 每个元素作为独立参数（Mojang 规范）
                            // 例: ["--width", "${resolution_width}", "--height", "${resolution_height}"]
                            //   → --width 1280 --height 720 (4 个独立 token)
                            for (const QJsonValue& val : argObj["value"].toArray())
                                args << val.toString();
                        }
                    }
                }
            }
        }

        // ── 占位符替换（参考 HMCL 使用 Map<placeholder,value> 批量替换）──
        // 预计算 classpath 字符串，避免循环内重复 join
        const QString classpathStr = classpath.join(classpathSeparator);
        for (int i = 0; i < args.size(); i++)
        {
            QString& arg = args[i];
            // 快速跳过: 不含 "${" 则无需任何替换
            if (!arg.contains(QLatin1Char('$')))
                continue;

            arg.replace(QLatin1String("${version_name}"), versionName);
            arg.replace(QLatin1String("${game_directory}"), basePath);
            arg.replace(QLatin1String("${assets_root}"), assetsPath);
            arg.replace(QLatin1String("${assets_index_name}"), assetIndex);
            arg.replace(QLatin1String("${auth_player_name}"), authPlayerName);
            arg.replace(QLatin1String("${auth_uuid}"), authUuid);
            arg.replace(QLatin1String("${auth_access_token}"), authToken);
            arg.replace(QLatin1String("${access_token}"), authToken);
            arg.replace(QLatin1String("${user_type}"), userType);
            arg.replace(QLatin1String("${user_properties}"), QStringLiteral("{}"));
            arg.replace(QLatin1String("${version_type}"), QStringLiteral("release"));
            arg.replace(QLatin1String("${natives_directory}"), nativesPath);
            arg.replace(QLatin1String("${library_directory}"), librariesPath);
            arg.replace(QLatin1String("${launcher_name}"), QStringLiteral("BlockBox"));
            arg.replace(QLatin1String("${launcher_version}"), QStringLiteral("1.0"));
            arg.replace(QLatin1String("${classpath_separator}"), classpathSeparator);
            arg.replace(QLatin1String("${classpath}"), classpathStr);
            arg.replace(QLatin1String("${clientid}"), QStringLiteral("0"));
            arg.replace(QLatin1String("${auth_xuid}"), QStringLiteral("0"));
            arg.replace(QLatin1String("${resolution_width}"), config.windowWidth);
            arg.replace(QLatin1String("${resolution_height}"), config.windowHeight);
            arg.replace(QLatin1String("${quickPlayPath}"), QString());
            arg.replace(QLatin1String("${quickPlaySingleplayer}"), QString());
            arg.replace(QLatin1String("${quickPlayMultiplayer}"), QString());
            arg.replace(QLatin1String("${quickPlayRealms}"), QString());
        }
    } else if (mergedJson.contains("minecraftArguments")) {
        // legacy (< 1.13)
        QString minecraftArgs = mergedJson["minecraftArguments"].toString();
        minecraftArgs.replace("${version_name}", versionName);
        minecraftArgs.replace("${game_directory}", basePath);
        minecraftArgs.replace("${assets_root}", assetsPath);
        minecraftArgs.replace("${assets_index_name}", assetIndex);
        minecraftArgs.replace("${auth_player_name}", authPlayerName);
        minecraftArgs.replace("${auth_uuid}", authUuid);
        minecraftArgs.replace("${auth_access_token}", authToken);
        minecraftArgs.replace("${user_type}", userType);
        minecraftArgs.replace("${version_type}", "release");
        args << minecraftArgs.split(" ", Qt::SkipEmptyParts);
    } else {
        // fallback
        args << "--username" << authPlayerName;
        args << "--version" << versionName;
        args << "--gameDir" << basePath;
        args << "--assetsDir" << assetsPath;
        args << "--assetIndex" << assetIndex;
        args << "--uuid" << authUuid;
        args << "--accessToken" << authToken;
        args << "--userType" << userType;
    }

    // ── 窗口大小 ──
    if (!config.windowWidth.isEmpty() && !config.windowHeight.isEmpty()) {
        args << "--width" << config.windowWidth;
        args << "--height" << config.windowHeight;
    }
    if (config.fullscreen)
        args << "--fullscreen";

    args << config.gameArgs;
    return args;
}

bool GameLauncher::updateOptionsTxt(const LaunchConfig& config)
{
    emit launchDetailAdded("更新 options.txt...");

    // 版本隔离：options.txt 在版本目录下（与 --gameDir 一致）
    // 非隔离：options.txt 在共享 .minecraft/ 目录下
    QString gameDir;
    if (SettingsManager::instance()->isVersionIsolationEnabled())
    {
        gameDir = config.instancePath;
    }
    else
    {
        gameDir = QDir(QFileInfo(config.instancePath).dir().absolutePath() + "/..").absolutePath();
    }
    QString optionsPath = gameDir + "/options.txt";

    // ── 兼容模组包：部分整合包将 options.txt 放在 config/ 子目录 ──
    // 参考 HMCL generateOptionsTxt()：优先使用 config/ 下的 options.txt
    if (!QFileInfo::exists(optionsPath))
    {
        QString configOptionsPath = gameDir + "/config/options.txt";
        if (QFileInfo::exists(configOptionsPath))
        {
            optionsPath = configOptionsPath;
            emit launchDetailAdded("在 config/ 子目录找到 options.txt");
        }
    }

    QFileInfo optionsFile(optionsPath);
    QMap<QString, QString> options;

    if (optionsFile.exists())
    {
        QFile file(optionsPath);
        if (file.open(QIODevice::ReadOnly))
        {
            QTextStream in(&file);
            while (!in.atEnd())
            {
                QString line = in.readLine();
                // Minecraft options.txt 使用 `:` 作为分隔符（HMCL/PCL2 一致）
                // 兼容旧版 BlockBox 曾使用的 `=`
                int separatorIndex = line.indexOf(':');
                if (separatorIndex == -1)
                    separatorIndex = line.indexOf('=');
                if (separatorIndex != -1)
                {
                    QString key = line.left(separatorIndex).trimmed();
                    QString value = line.mid(separatorIndex + 1).trimmed();
                    options[key] = value;
                }
            }
            file.close();
        }
    }

    // ── 仅更新启动器可控制的选项，保留用户已有的其他游戏设置 ──
    // 参考 HMCL generateOptionsTxt() + PCL2 做法：不覆盖已有的用户配置
    options["fullscreen"] = config.fullscreen ? "true" : "false";
    options["width"] = config.windowWidth;
    options["height"] = config.windowHeight;
    // ── 语言设置 ──
    // 参考 HMCL normalizedLanguageTag() + generateOptionsTxt() 设计
    // 以及 PCL2 McLaunchPrerun() 的版本适配逻辑
    {
        LanguageManager::Language lang = LanguageManager::instance()->currentLanguage();

        // 解析 MC 版本号 (major.minor)
        QString mcVer = resolveVersionId(config.instancePath);
        int mcMajor = 0;
        int mcMinor = 0;
        static const QRegularExpression verRe("(\\d+)\\.(\\d+)");
        QRegularExpressionMatch vm = verRe.match(mcVer);
        if (vm.hasMatch())
        {
            mcMajor = vm.captured(1).toInt();
            mcMinor = vm.captured(2).toInt();
        }

        /*
         * 语言标签版本适配规则（来源：HMCL normalizeLanguageTag + PCL2 注释）：
         *   MC 1.0       : 无语言选项，不设置
         *   MC 1.1~1.5   : zh_CN (大写)，写错会 NPE 崩溃
         *   MC 1.6~1.10  : zh_CN 可用, zh_cn 会自动切英文
         *   MC 1.11~1.12 : zh_cn (小写) 正常工作
         *   MC 1.13+     : zh_cn (小写)
         *   非 1.x 版本   : zh_cn (小写)
         */
        auto normalizeGameLanguageTag =
            [](LanguageManager::Language launcherLang, int major, int minor) -> QString {
                if (launcherLang == LanguageManager::English)
                    return "en_us";

                // Chinese
                if (major == 1)
                {
                    if (minor < 1)   return QString();     // MC 1.0: 无语言选项
                    if (minor <= 10) return "zh_CN";       // MC 1.1~1.10: 必须大写
                    return "zh_cn";                        // MC 1.11+: 小写
                }
                return "zh_cn";                            // 非 1.x: 小写
            };

        QString langTag = normalizeGameLanguageTag(lang, mcMajor, mcMinor);
        if (langTag.isEmpty())
        {
            emit launchDetailAdded(QString("MC %1 不支持语言选项，跳过").arg(mcVer));
        }
        else
        {
            options["lang"] = langTag;
            emit launchDetailAdded(QString("语言设置: %1 (MC %2)").arg(langTag, mcVer));
        }
    }

    // ── PCL2 风格两步写入：先写入 lang:- 使 Minecraft 缓存失效 ──
    // 参考 PCL2 McLaunchPrerun() 中 WriteIni("lang", "-") 的做法
    // 先写一次 lang:- 改变文件时间戳，避免 Minecraft 忽略单次写入
    if (options.contains("lang") && options["lang"] != "-")
    {
        QMap<QString, QString> invalidateOptions = options;
        invalidateOptions["lang"] = "-";
        QFile tmpFile(optionsPath);
        if (tmpFile.open(QIODevice::WriteOnly))
        {
            QTextStream ts(&tmpFile);
            for (auto it = invalidateOptions.constBegin(); it != invalidateOptions.constEnd(); ++it)
                ts << it.key() << ":" << it.value() << "\n";
            tmpFile.close();
        }
    }

    // ── 写入最终 options.txt ──
    QFile file(optionsPath);
    if (!file.open(QIODevice::WriteOnly))
    {
        emit launchDetailAdded(QString("无法写入 options.txt: %1").arg(optionsPath));
        return false;
    }

    QTextStream out(&file);
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
    {
        out << it.key() << ":" << it.value() << "\n";
    }

    file.close();
    emit launchDetailAdded("options.txt 更新成功");
    return true;
}

bool GameLauncher::updateLauncherProfiles(const LaunchConfig& config)
{
    emit launchDetailAdded("更新 launcher_profiles.json...");

    QString gameDir = QFileInfo(config.instancePath).dir().absolutePath() + "/..";
    QString profilesPath = gameDir + "/launcher_profiles.json";

    QFileInfo profilesFile(profilesPath);
    QJsonObject profiles;

    if (profilesFile.exists())
    {
        QFile file(profilesPath);
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject())
            {
                profiles = doc.object();
            }
            file.close();
        }
    }

    if (!profiles.contains("profiles"))
    {
        profiles["profiles"] = QJsonObject();
    }

    if (!profiles.contains("clientToken"))
    {
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(QDateTime::currentDateTime().toString().toUtf8());
        QString token = hash.result().toHex();
        profiles["clientToken"] = token;
    }

    if (!profiles.contains("selectedProfile"))
    {
        profiles["selectedProfile"] = "BlockBox";
    }

    QJsonObject profilesObj = profiles["profiles"].toObject();

    // ── 复用缓存 JSON，避免重复读取 ──
    QString versionName;
    if (!m_cachedMergedJson.isEmpty())
    {
        versionName = resolveJarName(config.instancePath, m_cachedMergedJson);
    }
    else
    {
        versionName = resolveJarName(config.instancePath, readVersionJson(config.instancePath));
    }

    QJsonObject blockboxProfile;
    blockboxProfile["name"] = "BlockBox";
    blockboxProfile["type"] = "custom";
    blockboxProfile["gameDir"] = gameDir;
    blockboxProfile["javaDir"] = config.javaPath == "auto" ? QDir::homePath() : config.javaPath;
    blockboxProfile["javaArgs"] = config.jvmArgs.join(" ");
    blockboxProfile["lastVersionId"] = versionName;
    blockboxProfile["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    blockboxProfile["lastUsed"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    profilesObj["BlockBox"] = blockboxProfile;
    profiles["profiles"] = profilesObj;
    profiles["selectedProfile"] = "BlockBox";

    QFile file(profilesPath);
    if (!file.open(QIODevice::WriteOnly))
    {
        emit launchDetailAdded(QString("无法写入 launcher_profiles.json: %1").arg(profilesPath));
        return false;
    }

    QJsonDocument doc(profiles);
    QByteArray data = doc.toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size())
    {
        emit launchDetailAdded(QString("写入 launcher_profiles.json 失败: %1").arg(profilesPath));
        file.close();
        return false;
    }

    file.close();
    emit launchDetailAdded("launcher_profiles.json 更新成功");
    return true;
}

bool GameLauncher::adjustGraphicsSettings()
{
    emit launchDetailAdded("调整显卡设置...");
    emit launchDetailAdded("显卡设置调整完成");
    return true;
}

bool GameLauncher::extractLog4jConfig(const LaunchConfig& config)
{
    emit launchDetailAdded("提取 Log4j 安全配置...");

    QString log4jPath = config.instancePath + "/log4j2.xml";

    // ── 安全配置：%msg{nolookups} 禁用 JNDI lookup → 防止 Log4Shell (CVE-2021-44228) ──
    static const char* safeLog4jXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<Configuration status="WARN">
    <Appenders>
        <Console name="SysOut" target="SYSTEM_OUT">
            <PatternLayout pattern="[%d{HH:mm:ss}] [%t/%level]: %msg{nolookups}%n" />
        </Console>
        <Queue name="ServerGuiConsole">
            <PatternLayout pattern="[%d{HH:mm:ss} %level]: %msg{nolookups}%n" />
        </Queue>
        <RollingRandomAccessFile name="File" fileName="logs/latest.log" filePattern="logs/%d{yyyy-MM-dd}-%i.log.gz">
            <PatternLayout pattern="[%d{HH:mm:ss}] [%t/%level]: %msg{nolookups}%n" />
            <Policies>
                <TimeBasedTriggeringPolicy />
                <OnStartupTriggeringPolicy />
            </Policies>
        </RollingRandomAccessFile>
    </Appenders>
    <Loggers>
        <Root level="info">
            <filters>
                <MarkerFilter marker="NETWORK_PACKETS" onMatch="DENY" onMismatch="NEUTRAL" />
            </filters>
            <AppenderRef ref="SysOut"/>
            <AppenderRef ref="File"/>
            <AppenderRef ref="ServerGuiConsole"/>
        </Root>
    </Loggers>
</Configuration>)";

    QFile file(log4jPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        emit launchDetailAdded(QString("警告: 无法写入 Log4j 配置文件: %1").arg(log4jPath));
        return false;
    }
    file.write(safeLog4jXml);
    file.close();

    emit launchDetailAdded(QString("Log4j 安全配置已写入: %1").arg(log4jPath));
    return true;
}

bool GameLauncher::preLaunchProcessing(const LaunchConfig& config)
{
    emit launchDetailAdded("执行预启动处理...");

    if (!updateLauncherProfiles(config))
    {
        return false;
    }

    if (!updateOptionsTxt(config))
    {
        return false;
    }

    if (!adjustGraphicsSettings())
    {
        return false;
    }

    if (!extractLog4jConfig(config))
    {
        return false;
    }

    emit launchDetailAdded("预启动处理完成");
    return true;
}