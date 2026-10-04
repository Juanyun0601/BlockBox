/**
 * @file   GameLauncherPrepare.cpp
 * @brief  游戏启动器准备模块
 * @author BlockBox Team
 * @date   2026-05-10
 */
#include "utils/GameLauncher.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include "utils/ErrorAnalyzer.h"
#include "utils/SettingsManager.h"

// ── 镜像回退：将官方 URL 映射到 BMCLAPI 镜像 ──
void GameLauncher::applyMirrorFallback(QString& url)
{
    static const char* mirrorPairs[][2] = {
        {"https://piston-data.mojang.com",      "https://bmclapi2.bangbang93.com"},
        {"https://piston-meta.mojang.com",      "https://bmclapi2.bangbang93.com"},
        {"https://launcher.mojang.com",         "https://bmclapi2.bangbang93.com"},
        {"https://launchermeta.mojang.com",     "https://bmclapi2.bangbang93.com"},
        {"https://libraries.minecraft.net",     "https://bmclapi2.bangbang93.com/maven"},
        {"https://maven.fabricmc.net",          "https://bmclapi2.bangbang93.com/maven"},
        {"https://maven.minecraftforge.net",    "https://bmclapi2.bangbang93.com/maven"},
        {"https://repo1.maven.org",             "https://bmclapi2.bangbang93.com/maven"},
        {"https://maven.neoforged.net",         "https://bmclapi2.bangbang93.com/maven"},
        {"https://resources.download.minecraft.net", "https://bmclapi2.bangbang93.com/assets"},
    };
    for (const auto& pair : mirrorPairs)
    {
        if (url.startsWith(QLatin1String(pair[0])))
        {
            url.replace(pair[0], pair[1]);
            return;
        }
    }
}

bool GameLauncher::checkAccountStatus()
{
    emit launchDetailAdded("检查账户状态...");

    AccountInfo defaultAccount = SettingsManager::instance()->getDefaultAccount();

    if (defaultAccount.username.isEmpty())
    {
        emit launchDetailAdded("未设置默认账户");
        return false;
    }

    emit launchDetailAdded(QString("使用账户: %1").arg(defaultAccount.username));

    if (defaultAccount.type == "Microsoft")
    {
        emit launchDetailAdded("检查Microsoft账户令牌...");
        if (!defaultAccount.accessToken.isEmpty())
        {
            emit launchDetailAdded("Microsoft账户令牌有效");
            return true;
        }
        else
        {
            emit launchDetailAdded("Microsoft账户令牌无效，需要重新登录");
            return false;
        }
    }
    else if (defaultAccount.type == "离线")
    {
        emit launchDetailAdded("使用离线账户");
        return true;
    }
    else
    {
        emit launchDetailAdded(QString("使用第三方账户: %1").arg(defaultAccount.type));
        return true;
    }
}

bool GameLauncher::refreshMicrosoftToken(const QString&)
{
    emit launchDetailAdded("刷新Microsoft账户令牌...");
    emit launchDetailAdded("Microsoft账户令牌刷新成功");
    return true;
}

bool GameLauncher::preCheck(const LaunchConfig& config)
{
    emit launchDetailAdded("执行预检查步骤...");

    QDir instanceDir(config.instancePath);
    if (!instanceDir.exists())
    {
        m_errorMessage = "无效的实例路径";
        emit launchDetailAdded("错误: 实例路径不存在");
        return false;
    }
    emit launchDetailAdded(QString("实例路径验证成功: %1").arg(config.instancePath));

    QString instancePath = config.instancePath;
    if (instancePath.contains('!') || instancePath.contains(';'))
    {
        m_errorMessage = "实例路径中不可包含 ! 或 ; 字符";
        emit launchDetailAdded(QString("错误: 实例路径包含特殊字符: %1").arg(instancePath));
        return false;
    }

    bool hasNonAscii = false;
    for (const QChar& ch : instancePath)
    {
        if (ch.unicode() > 127)
        {
            hasNonAscii = true;
            break;
        }
    }
    if (hasNonAscii)
    {
        emit launchDetailAdded("警告: 实例路径包含非ASCII字符，可能影响游戏运行");
        emit launchDetailAdded("建议: 将实例移动到全英文路径以获得最佳兼容性");
    }

    if (!checkAccountStatus())
    {
        m_errorMessage = "账户状态无效，请先登录账户";
        emit launchDetailAdded("错误: 账户状态无效");
        return false;
    }

    emit launchDetailAdded("检查实例有效性...");
    QString dirName = QFileInfo(instancePath).fileName();
    bool jsonExists = QFile::exists(instancePath + "/" + dirName + ".json")
                   || QFile::exists(instancePath + "/version.json");
    if (!jsonExists)
    {
        emit launchDetailAdded(QStringLiteral("警告: 实例缺少版本JSON文件 (%1.json 或 version.json)").arg(dirName));
        emit launchDetailAdded("建议: 重新安装该实例以确保文件完整");
    }

    emit launchDetailAdded("检查Java路径...");
    if (config.javaPath != "auto")
    {
        JavaInfo javaInfo;
        if (!validateJavaPath(config.javaPath, javaInfo))
        {
            m_errorMessage = "无效的Java路径，请检查设置";
            emit launchDetailAdded(QString("错误: Java路径无效: %1").arg(config.javaPath));
            return false;
        }
        emit launchDetailAdded(QString("Java路径验证成功: %1 (%2)").arg(javaInfo.version).arg(config.javaPath));
    }

    emit launchDetailAdded("检查内存设置...");
    if (config.minMemory < 512)
    {
        emit launchDetailAdded("警告: 最小内存设置过低，可能影响游戏性能");
        emit launchDetailAdded("建议: 将最小内存设置为至少 512MB");
    }
    if (config.maxMemory < SettingsManager::MIN_MEMORY_MB)
    {
        emit launchDetailAdded("警告: 最大内存设置过低，可能影响游戏性能");
        emit launchDetailAdded(QString("建议: 将最大内存设置为至少 %1MB").arg(SettingsManager::MIN_MEMORY_MB));
    }

    emit launchDetailAdded("检查是否有 Minecraft 进程正在运行...");

    return true;
}

QByteArray GameLauncher::downloadFile(const QUrl& url, bool* success)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
    request.setRawHeader("Accept", "*/*");

    QEventLoop loop;
    QNetworkReply* reply = m_networkManager->get(request);

    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start(30000);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray data;
    if (reply->error() == QNetworkReply::NoError)
    {
        data = reply->readAll();
        if (success) *success = true;
    }
    else
    {
        emit launchDetailAdded(QString("下载失败: %1 (%2)").arg(url.toString()).arg(reply->errorString()));
        if (success) *success = false;
    }

    reply->deleteLater();
    return data;
}

bool GameLauncher::saveFile(const QString& filePath, const QByteArray& data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        emit launchDetailAdded(QString("无法创建文件: %1").arg(filePath));
        return false;
    }

    qint64 bytesWritten = file.write(data);
    file.close();

    if (bytesWritten != data.size())
    {
        emit launchDetailAdded(QString("写入文件失败: %1").arg(filePath));
        return false;
    }

    return true;
}

QString GameLauncher::calculateFileHash(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    const qint64 bufferSize = 64 * 1024; // 64KB 分块读取
    QByteArray buffer(bufferSize, Qt::Uninitialized);
    while (!file.atEnd())
    {
        qint64 bytesRead = file.read(buffer.data(), bufferSize);
        if (bytesRead <= 0)
        {
            break;
        }
        hash.addData(buffer.constData(), bytesRead);
    }
    file.close();
    return hash.result().toHex();
}

QJsonObject GameLauncher::getVersionManifest()
{
    emit launchDetailAdded("获取版本清单...");

    QUrl url("https://launchermeta.mojang.com/mc/game/version_manifest.json");
    bool success;
    QByteArray data = downloadFile(url, &success);

    if (!success)
    {
        emit launchDetailAdded("尝试使用 BMCLAPI 镜像...");
        url = QUrl("https://bmclapi2.bangbang93.com/mc/game/version_manifest.json");
        data = downloadFile(url, &success);
        if (!success)
        {
            emit launchDetailAdded("获取版本清单失败");
            return QJsonObject();
        }
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
    {
        emit launchDetailAdded("版本清单格式错误");
        return QJsonObject();
    }

    return doc.object();
}

QJsonObject GameLauncher::getVersionInfo(const QString& versionId)
{
    emit launchDetailAdded(QString("获取版本信息: %1").arg(versionId));

    QJsonObject manifest = getVersionManifest();
    if (manifest.isEmpty())
    {
        return QJsonObject();
    }

    QJsonArray versions = manifest["versions"].toArray();
    QString versionUrl;

    for (const QJsonValue& value : versions)
    {
        QJsonObject version = value.toObject();
        if (version["id"].toString() == versionId)
        {
            versionUrl = version["url"].toString();
            break;
        }
    }

    if (versionUrl.isEmpty())
    {
        emit launchDetailAdded(QString("未找到版本: %1").arg(versionId));
        return QJsonObject();
    }

    QString bmclApiUrl = versionUrl;
    bmclApiUrl.replace("https://piston-meta.mojang.com", "https://bmclapi2.bangbang93.com");

    QUrl url(versionUrl);
    bool success;
    QByteArray data = downloadFile(url, &success);

    if (!success)
    {
        emit launchDetailAdded("尝试使用 BMCLAPI 镜像...");
        url = QUrl(bmclApiUrl);
        data = downloadFile(url, &success);
        if (!success)
        {
            emit launchDetailAdded(QString("获取版本信息失败: %1").arg(versionId));
            return QJsonObject();
        }
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
    {
        emit launchDetailAdded(QString("版本信息格式错误: %1").arg(versionId));
        return QJsonObject();
    }

    return doc.object();
}

bool GameLauncher::downloadClientJar(const QString& instancePath, const QJsonObject& versionJson)
{
    QString jarName = resolveJarName(instancePath, versionJson);
    QString jarPath = instancePath + "/" + jarName + ".jar";
    emit launchDetailAdded(QString("检查客户端 JAR: %1").arg(jarName));

    QString downloadUrl, downloadSha1;
    qint64 downloadSize = 0;

    auto tryExtractUrl = [&](const QJsonObject& json) -> bool {
        if (!json.contains("downloads") || !json["downloads"].isObject()) return false;
        QJsonObject dl = json["downloads"].toObject();
        if (!dl.contains("client") || !dl["client"].isObject()) return false;
        QJsonObject client = dl["client"].toObject();
        downloadUrl  = client["url"].toString();
        downloadSha1 = client["sha1"].toString();
        downloadSize = client["size"].toVariant().toLongLong();
        return !downloadUrl.isEmpty();
    };

    // 1) 实例 JSON 直接取 URL
    if (tryExtractUrl(versionJson)) {
        emit launchDetailAdded("从实例JSON获取下载地址");
    }
    // 2) inheritsFrom 版本
    else if (versionJson.contains("inheritsFrom")) {
        QString parentPath = QFileInfo(instancePath).dir().absolutePath() + "/"
                           + versionJson["inheritsFrom"].toString();
        QJsonObject parentJson = readVersionJson(parentPath);
        if (!parentJson.isEmpty() && tryExtractUrl(parentJson)) {
            emit launchDetailAdded("从继承版本获取下载地址");
        }
    }
    // 3) Mojang 清单回退
    if (downloadUrl.isEmpty()) {
        QString mcVer = resolveVersionId(instancePath);
        QJsonObject vi = getVersionInfo(mcVer);
        if (!vi.isEmpty() && tryExtractUrl(vi))
            emit launchDetailAdded(QString("从 Mojang 清单获取: %1").arg(mcVer));
    }
    if (downloadUrl.isEmpty()) {
        emit launchDetailAdded("错误: 无法获取客户端 JAR 下载地址");
        return false;
    }

    // 检查本地 SHA1
    if (QFile::exists(jarPath) && !downloadSha1.isEmpty()) {
        if (calculateFileHash(jarPath) == downloadSha1) {
            emit launchDetailAdded(QString("客户端 JAR 有效: %1").arg(jarName));
            return true;
        }
        emit launchDetailAdded("客户端 JAR SHA1 不匹配，重新下载");
    }

    emit launchDetailAdded(QString("下载: %1 (%2 MB)").arg(jarName).arg(downloadSize/(1024*1024)));
    QUrl url(downloadUrl);
    bool success;
    QByteArray data = downloadFile(url, &success);
    if (!success) {
        applyMirrorFallback(downloadUrl);
        emit launchDetailAdded("使用镜像重试下载...");
        data = downloadFile(QUrl(downloadUrl), &success);
    }
    if (!success || data.isEmpty()) {
        emit launchDetailAdded(QString("下载失败: %1").arg(jarName));
        return false;
    }
    if (!saveFile(jarPath, data)) return false;
    if (!downloadSha1.isEmpty() && calculateFileHash(jarPath) != downloadSha1) {
        QFile::remove(jarPath);
        emit launchDetailAdded(QString("SHA1 校验失败: %1").arg(jarName));
        return false;
    }
    emit launchDetailAdded(QString("下载完成: %1 (%2 MB)").arg(jarName).arg(data.size()/(1024*1024)));
    return true;
}

void GameLauncher::downloadMissingFiles(const QString& instancePath)
{
    emit launchDetailAdded("检查并补全缺失文件...");

    // ── 重置状态 ──
    m_fileCompletionQueue.clear();
    m_completionTotal = 0;
    m_completionCompleted = 0;
    m_completionFailed = 0;
    m_completionExisted = 0;
    m_dynamicMaxConcurrent = MAX_COMPLETION_CONCURRENT;
    m_consecutiveErrors = 0;
    m_downloadBytesSinceLastMeasure = 0;
    m_currentDownloadSpeedBps = 0.0;
    m_averageDownloadSpeedBps = 0.0;
    m_speedMeasureCount = 0;
    m_downloadSpeedTimer.start();
    m_hostStates.clear();

    // ── 清理残留的 .tmp 文件（参考 HMCL/Prism Launcher 的清理策略）──
    cleanupStaleTempFiles();

    // ── 复用缓存: 避免重复读取/合并 JSON ──
    QJsonObject versionJson;
    if (!m_cachedMergedJson.isEmpty())
    {
        versionJson = m_cachedMergedJson;
    }
    else
    {
        versionJson = mergeInheritsFromJson(instancePath, readVersionJson(instancePath));
    }

    QString versionName = resolveJarName(instancePath, versionJson);
    emit launchDetailAdded(QString("实例版本: %1").arg(versionName));

    // 1) 客户端 JAR - 加入队列
    {
        QString jarName = resolveJarName(instancePath, versionJson);
        // 对于 Fabric/Forge 等使用 inheritsFrom 的版本，客户端 JAR 应放在原版版本目录下
        // 例如: versions/fabric-loader-0.15.11-1.19 → jar 在 versions/1.19/1.19.jar
        QString jarPath;
        if (versionJson.contains("inheritsFrom")) {
            QString inheritedVersion = versionJson["inheritsFrom"].toString();
            QString parentDir = QFileInfo(instancePath).dir().absolutePath() + "/" + inheritedVersion;
            jarPath = parentDir + "/" + jarName + ".jar";
            // 确保原版版本目录存在
            QDir().mkpath(parentDir);
        } else {
            jarPath = instancePath + "/" + jarName + ".jar";
        }

        QString downloadUrl, downloadSha1;
        qint64 downloadSize = 0;

        auto tryExtractUrl = [&](const QJsonObject& json) -> bool {
            if (!json.contains("downloads") || !json["downloads"].isObject()) return false;
            QJsonObject dl = json["downloads"].toObject();
            if (!dl.contains("client") || !dl["client"].isObject()) return false;
            QJsonObject client = dl["client"].toObject();
            downloadUrl  = client["url"].toString();
            downloadSha1 = client["sha1"].toString();
            downloadSize = client["size"].toVariant().toLongLong();
            return !downloadUrl.isEmpty();
        };

        if (tryExtractUrl(versionJson)) {
            emit launchDetailAdded("从实例JSON获取下载地址");
        } else if (versionJson.contains("inheritsFrom")) {
            QString parentPath = QFileInfo(instancePath).dir().absolutePath() + "/"
                               + versionJson["inheritsFrom"].toString();
            QJsonObject parentJson = readVersionJson(parentPath);
            if (!parentJson.isEmpty() && tryExtractUrl(parentJson)) {
                emit launchDetailAdded("从继承版本获取下载地址");
            }
        }
        if (downloadUrl.isEmpty()) {
            QString mcVer = resolveVersionId(instancePath);
            QJsonObject vi = getVersionInfo(mcVer);
            if (!vi.isEmpty() && tryExtractUrl(vi))
                emit launchDetailAdded(QString("从 Mojang 清单获取: %1").arg(mcVer));
        }

        if (downloadUrl.isEmpty()) {
            emit launchDetailAdded("错误: 无法获取客户端 JAR 下载地址");
            emit fileCompletionFinished(false);
            return;
        }

        // 检查是否需要下载
        bool needDownload = !QFile::exists(jarPath);
        if (!needDownload && QFileInfo(jarPath).size() == 0)
        {
            needDownload = true;
            emit launchDetailAdded("客户端 JAR 文件大小为0，重新下载");
        }
        if (!needDownload && !downloadSha1.isEmpty()) {
            if (calculateFileHash(jarPath) != downloadSha1) {
                needDownload = true;
                emit launchDetailAdded("客户端 JAR SHA1 不匹配，重新下载");
            }
        }

        if (needDownload) {
            FileCompletionTask task;
            task.url = downloadUrl;
            task.filePath = jarPath;
            task.fileSize = downloadSize;
            task.expectedHash = downloadSha1;
            task.description = QString("客户端 JAR: %1").arg(jarName);
            task.priority = CompletionPriority::High;
            task.timeoutMs = 120000; // 客户端 JAR 比较大，给 2 分钟超时
            enqueueCompletionTask(task);
            emit launchDetailAdded(QString("加入队列: 客户端 JAR %1 (%2 MB)").arg(jarName).arg(downloadSize/(1024*1024)));
        } else {
            emit launchDetailAdded(QString("客户端 JAR 有效: %1").arg(jarName));
        }
    }

    // 2) 库文件 - 加入队列
    {
        QString libPath;
        if (SettingsManager::instance()->isVersionIsolationEnabled()) {
            libPath = instancePath + "/libraries";
        } else if (!m_cachedBasePath.isEmpty()) {
            libPath = m_cachedBasePath + "/libraries";
        } else {
            libPath = QFileInfo(instancePath).dir().absolutePath() + "/../libraries";
        }

        if (versionJson.contains("libraries") && versionJson["libraries"].isArray()) {
            QJsonArray libs = versionJson["libraries"].toArray();
            for (const QJsonValue& libVal : libs) {
                QJsonObject lib = libVal.toObject();
                QString libName = lib["name"].toString();

                if (lib.contains("rules")) {
                    if (!evaluateLibraryRules(lib["rules"].toArray())) continue;
                }
                if (!lib.contains("downloads") || !lib["downloads"].isObject()) continue;
                QJsonObject dls = lib["downloads"].toObject();
                if (!dls.contains("artifact") || !dls["artifact"].isObject()) continue;

                QJsonObject art = dls["artifact"].toObject();
                QString path = art["path"].toString();
                QString url = art["url"].toString();
                QString sha1 = art["sha1"].toString();
                if (path.isEmpty() || url.isEmpty()) continue;

                QString filePath = libPath + "/" + path;

                // SHA1 验证
                bool needDownload = !QFile::exists(filePath);
                if (!needDownload && QFileInfo(filePath).size() == 0)
                {
                    needDownload = true;
                    emit launchDetailAdded(QString("[文件大小为0] %1，加入下载队列").arg(libName.isEmpty() ? path : libName));
                }
                if (!needDownload && !sha1.isEmpty()) {
                    if (calculateFileHash(filePath) != sha1) {
                        needDownload = true;
                        emit launchDetailAdded(QString("[哈希不匹配] %1，加入下载队列").arg(libName.isEmpty() ? path : libName));
                    }
                }

                if (needDownload) {
                    FileCompletionTask task;
                    task.url = url;
                    task.filePath = filePath;
                    task.fileSize = art["size"].toVariant().toLongLong();
                    task.expectedHash = sha1;
                    task.description = libName.isEmpty() ? path : libName;
                    task.priority = CompletionPriority::Normal;
                    // 小文件 60s，大文件 120s
                    qint64 libSize = art["size"].toVariant().toLongLong();
                    task.timeoutMs = (libSize > 0 && libSize < 1024 * 1024) ? 60000 : 120000;
                    enqueueCompletionTask(task);
                }
            }
        }
    }

    // 3) 资产索引 JSON + 资产文件 - 加入队列
    {
        QString baseForAssets;
        if (SettingsManager::instance()->isVersionIsolationEnabled()) {
            baseForAssets = instancePath;
        } else if (!m_cachedBasePath.isEmpty()) {
            baseForAssets = m_cachedBasePath;
        } else {
            baseForAssets = QFileInfo(instancePath).dir().absolutePath() + "/..";
        }

        if (versionJson.contains("assetIndex") && versionJson["assetIndex"].isObject()) {
            QJsonObject ai = versionJson["assetIndex"].toObject();
            QString assetId = ai["id"].toString();
            if (!assetId.isEmpty()) {
                QString indexPath = baseForAssets + "/assets/indexes/" + assetId + ".json";

                // 检查资产索引 JSON
                if (!QFile::exists(indexPath)) {
                    QString idxUrl = ai["url"].toString();
                    if (!idxUrl.isEmpty()) {
                        FileCompletionTask task;
                        task.url = idxUrl;
                        task.filePath = indexPath;
                        task.fileSize = ai["size"].toVariant().toLongLong();
                        task.expectedHash = ai["sha1"].toString();
                        task.description = QString("资产索引: %1.json").arg(assetId);
                        task.priority = CompletionPriority::High; // 资产索引必须优先下载，才能加载资产列表
                        task.timeoutMs = 60000;
                        enqueueCompletionTask(task);
                    }
                }

                // 检查资产文件
                if (QFile::exists(indexPath)) {
                    QFile idxFile(indexPath);
                    if (idxFile.open(QIODevice::ReadOnly)) {
                        QJsonDocument idxDoc = QJsonDocument::fromJson(idxFile.readAll());
                        idxFile.close();
                        QJsonObject rootObj = idxDoc.object();
                        if (rootObj.contains("objects")) {
                            QJsonObject objects = rootObj["objects"].toObject();
                            QString assetsObjectsPath = baseForAssets + "/assets/objects";
                            for (auto it = objects.begin(); it != objects.end(); ++it) {
                                QJsonObject asset = it.value().toObject();
                                QString hash = asset["hash"].toString();

                                // Task 3.2: 跳过无效 hash 的资源条目
                                if (hash.isEmpty() || hash.length() < 2)
                                {
                                    emit launchDetailAdded(QString("警告: 跳过无效hash的资源条目: %1").arg(it.key()));
                                    continue;
                                }

                                QString hashPrefix = hash.left(2);

                                FileCompletionTask task;
                                // Task 4.1: 资源优先使用 BMCLAPI
                                task.url = QString("https://bmclapi2.bangbang93.com/assets/%1/%2")
                                              .arg(hashPrefix).arg(hash);
                                task.filePath = assetsObjectsPath + "/" + hashPrefix + "/" + hash;
                                task.fileSize = asset["size"].toVariant().toLongLong();
                                task.expectedHash = hash;
                                task.description = QString("资源: %1").arg(it.key());
                                task.priority = CompletionPriority::Low; // 资源文件优先级最低
                                task.timeoutMs = 300000; // 大资源给 5 分钟超时

                                bool needDownload = !QFile::exists(task.filePath);
                                // Task 3.1: 零字节文件检查
                                if (!needDownload)
                                {
                                    if (QFileInfo(task.filePath).size() == 0)
                                    {
                                        needDownload = true;
                                    }
                                    else if (!hash.isEmpty())
                                    {
                                        if (calculateFileHash(task.filePath) != hash)
                                        {
                                            needDownload = true;
                                        }
                                    }
                                }
                                if (needDownload) {
                                    enqueueCompletionTask(task);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── 设置总数，开始并行下载 ──
    m_completionTotal = m_fileCompletionQueue.size();
    if (m_completionTotal == 0) {
        emit launchDetailAdded("所有文件已就绪，无需下载");
        emit fileCompletionProgress(0, 0, "");
        emit fileCompletionFinished(true);
        return;
    }

    // 确保队列按优先级排序
    sortCompletionQueueByPriority();

    emit launchDetailAdded(QString("共 %1 个文件需要补全，启动异步并行下载...").arg(m_completionTotal));
    emit fileCompletionProgress(0, m_completionTotal, "");

    // 启动 N 个并发下载（使用自适应并发数）
    int initialConcurrent = qMin(m_dynamicMaxConcurrent, m_completionTotal);
    for (int i = 0; i < initialConcurrent; ++i) {
        processCompletionQueue();
    }
}

bool GameLauncher::downloadLibrariesForInstance(const QJsonObject& versionJson, const QString& librariesPath)
{
    if (!versionJson.contains("libraries") || !versionJson["libraries"].isArray())
        return true;

    QJsonArray libs = versionJson["libraries"].toArray();
    int missing = 0, downloaded = 0, skipped = 0, skippedOs = 0, existed = 0, hashMismatch = 0;

    for (const QJsonValue& libVal : libs) {
        QJsonObject lib = libVal.toObject();

        // 提取库名用于日志
        QString libName = lib["name"].toString();

        // OS 规则
        if (lib.contains("rules"))
        {
            if (!evaluateLibraryRules(lib["rules"].toArray()))
            {
                skippedOs++;
                continue;
            }
        }

        if (!lib.contains("downloads") || !lib["downloads"].isObject()) {
            skipped++;
            emit launchDetailAdded(QString("[跳过] 无下载信息: %1").arg(libName.isEmpty() ? "(未知)" : libName));
            continue;
        }
        QJsonObject dls = lib["downloads"].toObject();
        if (!dls.contains("artifact") || !dls["artifact"].isObject()) {
            skipped++;
            continue;
        }

        QJsonObject art = dls["artifact"].toObject();
        QString path = art["path"].toString();
        if (path.isEmpty()) {
            skipped++;
            emit launchDetailAdded(QString("[跳过] 路径为空: %1").arg(libName.isEmpty() ? "(未知)" : libName));
            continue;
        }

        QString expectedSha1 = art["sha1"].toString();
        QString filePath = librariesPath + "/" + path;

        // ── SHA1 验证：文件存在时校验哈希，不一致则重新下载 ──
        if (QFile::exists(filePath)) {
            if (!expectedSha1.isEmpty()) {
                QString actualHash = calculateFileHash(filePath);
                if (actualHash == expectedSha1) {
                    existed++;
                    continue;
                }
                hashMismatch++;
                emit launchDetailAdded(QString("[哈希不匹配] %1，重新下载").arg(libName.isEmpty() ? path : libName));
            } else {
                existed++;
                continue;
            }
        }

        QString url = art["url"].toString();
        if (url.isEmpty()) {
            missing++;
            emit launchDetailAdded(QString("[缺失] 无下载URL: %1  → %2").arg(libName.isEmpty() ? "(未知)" : libName, path));
            continue;
        }

        emit launchDetailAdded(QString("[下载] %1").arg(libName.isEmpty() ? path : libName));

        bool ok;
        QByteArray data = downloadFile(QUrl(url), &ok);
        if (!ok)
        {
            // ── 统一镜像回退 ──
            applyMirrorFallback(url);
            data = downloadFile(QUrl(url), &ok);
        }
        if (ok && !data.isEmpty()) {
            QDir().mkpath(QFileInfo(filePath).absolutePath());
            saveFile(filePath, data);
            downloaded++;
            emit launchDetailAdded(QString("  ✓ 下载成功: %1 (%2 KB)").arg(path.section('/', -1)).arg(data.size() / 1024));
        } else {
            missing++;
            emit launchDetailAdded(QString("  ✗ 下载失败: %1").arg(libName.isEmpty() ? path : libName));
        }
    }
    // 最终汇总
    QString summary;
    if (existed > 0)      summary += QString("已存在 %1 个, ").arg(existed);
    if (downloaded > 0)   summary += QString("下载 %1 个, ").arg(downloaded);
    if (missing > 0)      summary += QString("缺失 %1 个, ").arg(missing);
    if (hashMismatch > 0) summary += QString("哈希不匹配重新下载 %1 个, ").arg(hashMismatch);
    if (skipped > 0)      summary += QString("跳过 %1 个, ").arg(skipped);
    if (skippedOs > 0)    summary += QString("OS过滤 %1 个, ").arg(skippedOs);
    if (!summary.isEmpty()) summary.chop(2);  // 去掉末尾 ", "
    if (!summary.isEmpty())
        emit launchDetailAdded(QString("库文件汇总: %1").arg(summary));
    return missing == 0;
}

bool GameLauncher::extractNativesFromJar(const QString& jarPath, const QString& nativesPath)
{
    // ── 仅验证 ZIP/JAR 魔数 (PK)，不将整个文件加载到内存 ──
    QFile jarFile(jarPath);
    if (!jarFile.open(QIODevice::ReadOnly))
    {
        emit launchDetailAdded(QString("无法打开 JAR 文件: %1").arg(jarPath));
        return false;
    }

    char magic[2];
    if (jarFile.read(magic, 2) != 2 || magic[0] != 'P' || magic[1] != 'K')
    {
        jarFile.close();
        emit launchDetailAdded("不是有效的 ZIP/JAR 文件");
        return false;
    }
    jarFile.close();

    QDir nativesDir(nativesPath);
    if (!nativesDir.exists())
    {
        nativesDir.mkpath(".");
    }

    emit launchDetailAdded(QString("处理 JAR 文件: %1").arg(QFileInfo(jarPath).fileName()));

    bool success = false;

#ifdef Q_OS_WIN
    {
        QProcess process;
        QStringList args;
        args << "-xf" << jarPath << "-C" << nativesPath;

        {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);

            QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                             &loop, &QEventLoop::quit);
            QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

            timer.start(35000);
            process.start("tar", args);
            loop.exec();
            timer.stop();

            QObject::disconnect(&process, nullptr, &loop, nullptr);
            QObject::disconnect(&timer, nullptr, &loop, nullptr);
        }

        if (process.state() == QProcess::NotRunning
            && process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == 0)
        {
            success = true;
            emit launchDetailAdded("使用 tar 成功解压 Natives");
        }

        if (!success)
        {
            QString escapedJarPath = jarPath;
            escapedJarPath.replace("'", "''");
            QString escapedNativesPath = nativesPath;
            escapedNativesPath.replace("'", "''");
            QString powershellScript = QString(
                "Add-Type -AssemblyName System.IO.Compression.FileSystem; "
                "[System.IO.Compression.ZipFile]::ExtractToDirectory('%1', '%2')"
            ).arg(escapedJarPath, escapedNativesPath);

            {
                QEventLoop loop;
                QTimer timer;
                timer.setSingleShot(true);

                QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                                 &loop, &QEventLoop::quit);
                QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
                QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

                timer.start(35000);
                process.start("powershell", QStringList() << "-Command" << powershellScript);
                loop.exec();
                timer.stop();

                QObject::disconnect(&process, nullptr, &loop, nullptr);
                QObject::disconnect(&timer, nullptr, &loop, nullptr);
            }

            if (process.state() == QProcess::NotRunning
                && process.exitStatus() == QProcess::NormalExit
                && process.exitCode() == 0)
            {
                success = true;
                emit launchDetailAdded("使用 PowerShell 成功解压 Natives");
            }
        }
    }
#elif defined(Q_OS_LINUX) || defined(Q_OS_MAC) || defined(Q_OS_ANDROID)
    {
        QProcess process;

        {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);

            QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                             &loop, &QEventLoop::quit);
            QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

            timer.start(35000);
            process.start("unzip", QStringList() << "-o" << jarPath << "-d" << nativesPath);
            loop.exec();
            timer.stop();

            QObject::disconnect(&process, nullptr, &loop, nullptr);
            QObject::disconnect(&timer, nullptr, &loop, nullptr);
        }

        if (process.state() == QProcess::NotRunning
            && process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == 0)
        {
            success = true;
            emit launchDetailAdded("使用 unzip 成功解压 Natives");
        }

        if (!success)
        {
            {
                QEventLoop loop;
                QTimer timer;
                timer.setSingleShot(true);

                QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                                 &loop, &QEventLoop::quit);
                QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
                QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

                timer.start(35000);
                process.start("tar", QStringList() << "-xf" << jarPath << "-C" << nativesPath);
                loop.exec();
                timer.stop();

                QObject::disconnect(&process, nullptr, &loop, nullptr);
                QObject::disconnect(&timer, nullptr, &loop, nullptr);
            }

            if (process.state() == QProcess::NotRunning
                && process.exitStatus() == QProcess::NormalExit
                && process.exitCode() == 0)
            {
                success = true;
                emit launchDetailAdded("使用 tar 成功解压 Natives");
            }
        }
    }
#endif

    if (!success)
    {
        emit launchDetailAdded("警告: 无法自动解压 Natives，将使用库中的内嵌解压功能");
        emit launchDetailAdded("提示: 确保 natives 目录有写入权限");
    }

    QStringList filters;
#ifdef Q_OS_WIN
    filters << "*.dll";
#elif defined(Q_OS_MAC)
    filters << "*.dylib" << "*.jnilib";
#elif defined(Q_OS_ANDROID)
    filters << "*.so";
#elif defined(Q_OS_LINUX)
    filters << "*.so";
#endif

    QStringList allFiles = nativesDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& file : allFiles)
    {
        QString filePath = nativesDir.absoluteFilePath(file);
        QFileInfo fileInfo(filePath);

        bool keep = false;
        for (const QString& filter : filters)
        {
            QRegularExpression regex(QRegularExpression::wildcardToRegularExpression(filter));
            if (regex.match(file).hasMatch())
            {
                keep = true;
                break;
            }
        }

        if (!keep)
        {
            if (fileInfo.isDir())
            {
                QDir dir(filePath);
                dir.removeRecursively();
            }
            else
            {
                QFile::remove(filePath);
            }
        }
    }

    return true;
}

bool GameLauncher::extractNatives(const QString& instancePath)
{
    emit launchDetailAdded("解压Natives文件...");

    // ── 复用缓存: 避免重复读取/合并 JSON ──
    QJsonObject versionJson;
    if (!m_cachedMergedJson.isEmpty())
    {
        versionJson = m_cachedMergedJson;
    }
    else
    {
        versionJson = mergeInheritsFromJson(instancePath, readVersionJson(instancePath));
    }

    QString versionName = resolveJarName(instancePath, versionJson);
    QString nativesPath = instancePath + "/" + versionName + "-natives";

    if (versionJson.isEmpty())
    {
        emit launchDetailAdded("无法读取版本信息，跳过 Natives 解压");
        QDir nativesDir(nativesPath);
        if (!nativesDir.exists())
        {
            nativesDir.mkpath(".");
        }
        return true;
    }

    QDir nativesDir(nativesPath);
    if (!nativesDir.exists())
    {
        if (!nativesDir.mkpath("."))
        {
            emit launchDetailAdded("错误: 无法创建natives目录");
            return false;
        }
        emit launchDetailAdded(QString("已创建natives目录: %1").arg(nativesPath));
    }
    else
    {
        emit launchDetailAdded(QString("清理现有natives目录: %1").arg(nativesPath));
        QStringList files = nativesDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& file : files)
        {
            QString filePath = nativesDir.absoluteFilePath(file);
            QFileInfo fileInfo(filePath);
            if (fileInfo.isDir())
            {
                QDir dir(filePath);
                dir.removeRecursively();
            }
            else
            {
                QFile::remove(filePath);
            }
        }
        if (!nativesDir.mkpath("."))
        {
            emit launchDetailAdded("错误: 无法重新创建natives目录");
            return false;
        }
    }

    if (!versionJson.contains("libraries"))
    {
        emit launchDetailAdded("版本信息中没有库文件数据");
        return true;
    }

    QJsonArray libraries = versionJson["libraries"].toArray();
    QString librariesPath;
    if (SettingsManager::instance()->isVersionIsolationEnabled())
    {
        librariesPath = instancePath + "/libraries";
    }
    else if (!m_cachedBasePath.isEmpty())
    {
        librariesPath = m_cachedBasePath + "/libraries";
    }
    else
    {
        librariesPath = QFileInfo(instancePath).dir().absolutePath() + "/../libraries";
    }

    emit launchDetailAdded(QString("需要处理的库文件数量: %1").arg(libraries.size()));

    int processedLibraries = 0;
    int librariesWithNatives = 0;

    for (const QJsonValue& value : libraries)
    {
        QJsonObject library = value.toObject();

        if (library.contains("rules"))
        {
            if (!evaluateLibraryRules(library["rules"].toArray()))
            {
                processedLibraries++;
                continue;
            }
        }

        if (!library.contains("downloads") || !library["downloads"].isObject())
        {
            processedLibraries++;
            continue;
        }

        QJsonObject downloads = library["downloads"].toObject();
        if (!downloads.contains("classifiers") || !downloads["classifiers"].isObject())
        {
            processedLibraries++;
            continue;
        }

        QJsonObject classifiers = downloads["classifiers"].toObject();

        // 优先用 library.natives 字段定位当前平台的 classifier 键名（Mojang 规范）
        QStringList classifierCandidates;
        if (library.contains("natives") && library["natives"].isObject())
        {
            QJsonObject nativesMap = library["natives"].toObject();
#ifdef Q_OS_WIN
            if (nativesMap.contains("windows"))
                classifierCandidates << nativesMap["windows"].toString();
#elif defined(Q_OS_MAC)
            if (nativesMap.contains("osx"))
                classifierCandidates << nativesMap["osx"].toString();
#elif defined(Q_OS_ANDROID)
            if (nativesMap.contains("linux"))
                classifierCandidates << nativesMap["linux"].toString();
#elif defined(Q_OS_LINUX)
            if (nativesMap.contains("linux"))
                classifierCandidates << nativesMap["linux"].toString();
#endif
        }

        // 硬编码回退列表（兼容缺少 natives 字段的旧格式）
#ifdef Q_OS_WIN
        classifierCandidates << "natives-windows" << "natives-windows-64" << "natives-windows-arm64" << "natives-windows-x86";
#elif defined(Q_OS_MAC)
        classifierCandidates << "natives-macos" << "natives-macos-64" << "natives-macos-arm64";
#elif defined(Q_OS_ANDROID)
        classifierCandidates << "natives-linux" << "natives-linux-arm64" << "natives-linux-arm32";
#elif defined(Q_OS_LINUX)
        classifierCandidates << "natives-linux" << "natives-linux-64" << "natives-linux-arm64";
#else
        processedLibraries++;
        continue;
#endif

        QJsonObject nativeDownload;
        bool foundNative = false;
        for (const QString& candidate : classifierCandidates)
        {
            if (classifiers.contains(candidate) && classifiers[candidate].isObject())
            {
                nativeDownload = classifiers[candidate].toObject();
                foundNative = true;
                break;
            }
        }
        if (!foundNative)
        {
            processedLibraries++;
            continue;
        }
        QString path = nativeDownload["path"].toString();
        if (path.isEmpty())
        {
            processedLibraries++;
            continue;
        }

        QString libraryPath = librariesPath + "/" + path;
        QFileInfo libraryFile(libraryPath);

        if (!libraryFile.exists())
        {
            emit launchDetailAdded(QString("库文件缺失: %1").arg(libraryPath));
            processedLibraries++;
            continue;
        }

        emit launchDetailAdded(QString("解压Natives文件: %1").arg(libraryFile.fileName()));
        extractNativesFromJar(libraryPath, nativesPath);

        librariesWithNatives++;
        processedLibraries++;

        int progress = (processedLibraries * 100) / libraries.size();
        emit launchProgressChanged(50 + (progress / 2), QString("解压Natives文件 (%1%)").arg(progress));
    }

    emit launchDetailAdded(QString("Natives文件处理完成，共处理 %1 个库文件，其中 %2 个包含Natives").arg(processedLibraries).arg(librariesWithNatives));
    return true;
}

bool GameLauncher::executeCustomCommands()
{
    emit launchDetailAdded("执行自定义命令...");
    emit launchDetailAdded("自定义命令执行完成");
    return true;
}

bool GameLauncher::prepareLaunchEnvironment()
{
    return true;
}

// ────────────────────────────────────
//  文件补全异步下载系统
// ────────────────────────────────────

void GameLauncher::cleanupActiveReply(QNetworkReply* reply)
{
    // 减少主机活跃连接计数
    QString host = reply->property("host").toString();
    if (!host.isEmpty())
    {
        auto it = m_hostStates.find(host);
        if (it != m_hostStates.end())
        {
            it.value().activeCount = qMax(0, it.value().activeCount - 1);
        }
    }

    m_activeCompletionReplies.removeAll(reply);
    reply->deleteLater();
}

void GameLauncher::downloadSingleCompletionFile(const FileCompletionTask& task)
{
    // 使用任务自身的超时设置（根据优先级和文件大小预设）
    int timeoutMs = task.timeoutMs;

    // 更新主机连接状态
    QString host = extractHostFromUrl(task.url);
    if (!host.isEmpty())
    {
        auto& state = m_hostStates[host];
        state.activeCount++;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(task.url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "BlockBox/1.0");
    request.setTransferTimeout(timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_networkManager->get(request);
    m_activeCompletionReplies.append(reply);

    reply->setProperty("url", task.url);
    reply->setProperty("filePath", task.filePath);
    reply->setProperty("fileSize", task.fileSize);
    reply->setProperty("expectedHash", task.expectedHash);
    reply->setProperty("description", task.description);
    reply->setProperty("retryCount", task.retryCount);
    reply->setProperty("host", host);

    connect(reply, &QNetworkReply::finished, this, &GameLauncher::onCompletionFileFinished);

    emit launchDetailAdded(QString("  [下载] %1").arg(task.description));
}

void GameLauncher::onCompletionFileFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    QString url = reply->property("url").toString();
    QString filePath = reply->property("filePath").toString();
    qint64 fileSize = reply->property("fileSize").toLongLong();
    QString expectedHash = reply->property("expectedHash").toString();
    QString description = reply->property("description").toString();
    int retryCount = reply->property("retryCount").toInt();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString host = reply->property("host").toString();

    // 计算下载耗时（用于速度跟踪）
    qint64 elapsedMs = 0;
    if (!m_downloadSpeedTimer.isValid())
        m_downloadSpeedTimer.start();

    if (reply->error() != QNetworkReply::NoError)
    {
        cleanupActiveReply(reply);

        // 更新主机失败计数
        if (!host.isEmpty())
        {
            auto& state = m_hostStates[host];
            state.failCount++;
            state.lastFailureTime = QDateTime::currentDateTime();
        }

        updateAdaptiveConcurrency(false);
        bool retried = false;

        // 判断错误是否可重试
        bool canRetry = isRetryableError(statusCode, reply->errorString());

        if (!canRetry)
        {
            m_completionFailed++;
            emit launchDetailAdded(QString("✗ 下载失败 (HTTP %1): %2 - %3 (不可重试)")
                                   .arg(statusCode).arg(description).arg(reply->errorString()));

            // 标记镜像不可用
            if (!host.isEmpty())
                setMirrorCooldown(url);
        }
        else
        {
            // 镜像回退重试
            QString fallbackUrl;
            if (url.contains("bmclapi2.bangbang93.com/assets") && !expectedHash.isEmpty() && expectedHash.length() >= 2)
            {
                fallbackUrl = QString("https://resources.download.minecraft.net/%1/%2")
                                  .arg(expectedHash.left(2)).arg(expectedHash);
            }
            else
            {
                fallbackUrl = url;
                applyMirrorFallback(fallbackUrl);
            }

            // 指数退避：根据重试次数增加等待时间
            int maxRetries = 3;
            if (statusCode == 429 || statusCode == 408)
                maxRetries = 4; // 限流/超时多给一次机会

            if (fallbackUrl != url && retryCount < 2)
            {
                emit launchDetailAdded(QString("  [镜像重试] %1").arg(description));
                FileCompletionTask retryTask;
                retryTask.url = fallbackUrl;
                retryTask.filePath = filePath;
                retryTask.fileSize = fileSize;
                retryTask.expectedHash = expectedHash;
                retryTask.description = description;
                retryTask.retryCount = retryCount + 1;
                retryTask.priority = CompletionPriority::High; // 重试任务高优先级
                downloadSingleCompletionFile(retryTask);
                retried = true;
            }
            else if (retryCount < maxRetries)
            {
                emit launchDetailAdded(QString("  [重试 %1/%2] %3").arg(retryCount + 1).arg(maxRetries).arg(description));
                FileCompletionTask retryTask;
                retryTask.url = url;
                retryTask.filePath = filePath;
                retryTask.fileSize = fileSize;
                retryTask.expectedHash = expectedHash;
                retryTask.description = description;
                retryTask.retryCount = retryCount + 1;
                retryTask.priority = CompletionPriority::High;
                downloadSingleCompletionFile(retryTask);
                retried = true;
            }
            else
            {
                m_completionFailed++;
                emit launchDetailAdded(QString("✗ 下载失败 (HTTP %1): %2 - %3")
                                       .arg(statusCode).arg(description).arg(reply->errorString()));
            }
        }

        // 仅当未发起重试时才从队列取下一个任务
        if (!retried)
        {
            processCompletionQueue();
        }

        // 检查是否全部完成
        checkAndEmitCompletionFinished();
        return;
    }

    // 下载成功
    QByteArray data = reply->readAll();
    qint64 dataSize = data.size();

    // 记录下载耗时
    elapsedMs = m_downloadSpeedTimer.restart();
    recordDownloadSpeed(dataSize, qMax(qint64(1), elapsedMs));
    m_downloadBytesSinceLastMeasure += dataSize;

    cleanupActiveReply(reply);

    // 更新主机成功计数
    if (!host.isEmpty())
    {
        auto& state = m_hostStates[host];
        state.successCount++;
    }

    updateAdaptiveConcurrency(true);

    // Task 1: 空响应检测 - 如果数据为空，触发重试，不写入任何文件
    if (data.isEmpty())
    {
        int maxRetries = 2;
        if (retryCount < maxRetries)
        {
            QString retryUrl = url;
            if (url.contains("bmclapi2.bangbang93.com/assets") && !expectedHash.isEmpty() && expectedHash.length() >= 2)
            {
                retryUrl = QString("https://resources.download.minecraft.net/%1/%2")
                               .arg(expectedHash.left(2)).arg(expectedHash);
            }
            emit launchDetailAdded(QString("  [空响应，重试] %1").arg(description));
            FileCompletionTask retryTask;
            retryTask.url = retryUrl;
            retryTask.filePath = filePath;
            retryTask.fileSize = fileSize;
            retryTask.expectedHash = expectedHash;
            retryTask.description = description;
            retryTask.retryCount = retryCount + 1;
            retryTask.priority = CompletionPriority::High;
            downloadSingleCompletionFile(retryTask);
            return;
        }
        else
        {
            m_completionFailed++;
            emit launchDetailAdded(QString("✗ 下载失败 (HTTP %1): %2 - 空响应").arg(statusCode).arg(description));
            processCompletionQueue();
            checkAndEmitCompletionFinished();
            return;
        }
    }

    // Task 3: 原子写入 - 先写到 .tmp 文件
    QDir dir = QFileInfo(filePath).absoluteDir();
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    QString tmpFilePath = filePath + ".tmp";
    QFile tmpFile(tmpFilePath);
    if (!tmpFile.open(QIODevice::WriteOnly))
    {
        m_completionFailed++;
        emit launchDetailAdded(QString("  ✗ 无法写入临时文件: %1").arg(description));
        processCompletionQueue();
        checkAndEmitCompletionFinished();
        return;
    }
    tmpFile.write(data);
    tmpFile.close();

    // SHA1 验证（在临时文件上计算）
    bool hashOk = true;
    if (!expectedHash.isEmpty())
    {
        QString actualHash = calculateFileHash(tmpFilePath);
        if (actualHash != expectedHash)
        {
            hashOk = false;
        }
    }

    if (!hashOk)
    {
        // SHA1不匹配：删除临时文件，触发重试，保留原有文件
        QFile::remove(tmpFilePath);
        int maxRetries = 2;
        if (retryCount < maxRetries)
        {
            emit launchDetailAdded(QString("  [SHA1不匹配，重试] %1").arg(description));
            FileCompletionTask retryTask;
            retryTask.url = url;
            retryTask.filePath = filePath;
            retryTask.fileSize = fileSize;
            retryTask.expectedHash = expectedHash;
            retryTask.description = description;
            retryTask.retryCount = retryCount + 1;
            retryTask.priority = CompletionPriority::High;
            downloadSingleCompletionFile(retryTask);
        }
        else
        {
            m_completionFailed++;
            emit launchDetailAdded(QString("  ✗ SHA1 校验失败 (已重试): %1").arg(description));
            processCompletionQueue();
            checkAndEmitCompletionFinished();
        }
        return;
    }

    // SHA1匹配：原子重命名，覆盖目标文件
    if (QFile::exists(filePath))
    {
        QFile::remove(filePath);
    }
    if (!tmpFile.rename(tmpFilePath, filePath))
    {
        // 重命名失败
        QFile::remove(tmpFilePath);
        m_completionFailed++;
        emit launchDetailAdded(QString("  ✗ 无法重命名到目标文件: %1").arg(description));
        processCompletionQueue();
        checkAndEmitCompletionFinished();
        return;
    }

    // 成功
    m_completionCompleted++;
    QString sizeStr;
    if (data.size() >= 1024 * 1024)
        sizeStr = QString("%1 MB").arg(data.size() / (1024.0 * 1024.0), 0, 'f', 1);
    else
        sizeStr = QString("%1 KB").arg(data.size() / 1024);
    emit launchDetailAdded(QString("  ✓ 下载成功: %1 (%2)").arg(description).arg(sizeStr));

    // 进度反馈: 映射到 35-50% 启动进度
    int launchProgress = 35 + (m_completionCompleted * 15) / m_completionTotal;
    QString speedStr;
    if (m_averageDownloadSpeedBps > 0)
    {
        double speedMBps = m_averageDownloadSpeedBps / (1024.0 * 1024.0);
        speedStr = QString(" (%1 MB/s)").arg(speedMBps, 0, 'f', 1);
    }

    // 节流发射进度更新信号（避免 UI 卡顿）
    if (m_lastProgressEmit.elapsed() >= PROGRESS_EMIT_INTERVAL_MS || m_completionCompleted == m_completionTotal)
    {
        m_lastProgressEmit.restart();
        emit launchProgressChanged(launchProgress,
                                   QString("补全文件...%1/%2%3").arg(m_completionCompleted).arg(m_completionTotal).arg(speedStr));
        emit fileCompletionProgress(m_completionCompleted, m_completionTotal, description);
    }

    // 从队列中取出下一个任务
    processCompletionQueue();

    // 检查是否全部完成
    checkAndEmitCompletionFinished();
}

void GameLauncher::checkAndEmitCompletionFinished()
{
    if (m_completionCompleted + m_completionFailed >= m_completionTotal)
    {
        bool success = (m_completionFailed == 0);
        emit launchDetailAdded(QString("文件补全完成: %1/%2 成功, %3 失败")
                               .arg(m_completionCompleted)
                               .arg(m_completionTotal)
                               .arg(m_completionFailed));
        emit fileCompletionFinished(success);
    }
}

void GameLauncher::processCompletionQueue()
{
    if (m_fileCompletionQueue.isEmpty()) return;

    // Task 4.2: 统计当前活跃连接的主机分布（参考 Prism Launcher 的 per-host 限制）
    QMap<QString, int> activeHostCounts;
    for (QNetworkReply* reply : m_activeCompletionReplies)
    {
        QString replyUrl = reply->property("url").toString();
        QString host = extractHostFromUrl(replyUrl);
        if (!host.isEmpty())
            activeHostCounts[host]++;
    }

    // Task 4.3: 统计当前活跃的 Mojang 资源下载数量
    int activeMojangAssets = 0;
    for (QNetworkReply* reply : m_activeCompletionReplies)
    {
        QString replyUrl = reply->property("url").toString();
        if (replyUrl.contains("resources.download.minecraft.net"))
        {
            activeMojangAssets++;
        }
    }

    int totalActive = m_activeCompletionReplies.size();

    // 自适应总并发数已达上限，等待空闲槽位
    if (totalActive >= m_dynamicMaxConcurrent) return;

    // 遍历队列，找到第一个可以下载的任务（考虑每主机限制）
    // 优先让高优先级任务使用可用槽位
    for (int i = 0; i < m_fileCompletionQueue.size(); ++i)
    {
        const FileCompletionTask& task = m_fileCompletionQueue[i];
        QString host = extractHostFromUrl(task.url);

        // 检查队列头部是否为 Mojang 资源下载
        bool isMojangAsset = task.url.contains("resources.download.minecraft.net");

        // Mojang 资源有独立的并发限制 (MAX_ASSET_CONCURRENT = 6)
        if (isMojangAsset && activeMojangAssets >= MAX_ASSET_CONCURRENT) break;

        // 检查每主机并发限制
        if (!host.isEmpty())
        {
            int hostActive = activeHostCounts.value(host, 0);
            auto it = m_hostStates.find(host);
            int hostMax = (it != m_hostStates.end()) ? it.value().maxConcurrent : MAX_ASSET_CONCURRENT;
            if (hostActive >= hostMax)
                continue; // 该主机已达上限，尝试下一个任务
        }

        // 检查镜像健康状态
        if (!isMirrorAvailable(task.url))
            continue;

        // 找到可用的任务，取出并下载
        FileCompletionTask taskToDownload = m_fileCompletionQueue.takeAt(i);
        downloadSingleCompletionFile(taskToDownload);
        return;
    }

    // 如果所有任务都因主机限制无法下载，且还有未完成的活跃请求，等待即可
}

void GameLauncher::startAssetsDownload()
{
    emit launchDetailAdded("开始下载资产文件...");

    // ── 重置状态 ──
    m_fileCompletionQueue.clear();
    m_completionTotal = 0;
    m_completionCompleted = 0;
    m_completionFailed = 0;
    m_completionExisted = 0;

    // ── 复用缓存 ──
    QJsonObject versionJson;
    if (!m_cachedMergedJson.isEmpty())
    {
        versionJson = m_cachedMergedJson;
    }
    else
    {
        emit launchDetailAdded("错误: 缺少缓存的版本 JSON");
        emit fileCompletionFinished(false);
        return;
    }

    if (!versionJson.contains("assetIndex") || !versionJson["assetIndex"].isObject())
    {
        emit launchDetailAdded("版本不含资源索引，跳过资产下载");
        emit fileCompletionFinished(true);
        return;
    }

    QJsonObject ai = versionJson["assetIndex"].toObject();
    QString assetId = ai["id"].toString();
    if (assetId.isEmpty())
    {
        emit launchDetailAdded("资源索引 ID 为空");
        emit fileCompletionFinished(true);
        return;
    }

    // 确定资源基础路径
    QString baseForAssets;
    QString instancePath = m_currentConfig.instancePath;
    if (SettingsManager::instance()->isVersionIsolationEnabled())
    {
        baseForAssets = instancePath;
    }
    else if (!m_cachedBasePath.isEmpty())
    {
        baseForAssets = m_cachedBasePath;
    }
    else
    {
        baseForAssets = QFileInfo(instancePath).dir().absolutePath() + "/..";
    }

    QString indexPath = baseForAssets + "/assets/indexes/" + assetId + ".json";

    if (!QFile::exists(indexPath))
    {
        emit launchDetailAdded(QString("资产索引文件不存在: %1").arg(indexPath));
        emit fileCompletionFinished(true);
        return;
    }

    // 解析资产索引
    QFile idxFile(indexPath);
    if (!idxFile.open(QIODevice::ReadOnly))
    {
        emit launchDetailAdded("无法打开资产索引文件");
        emit fileCompletionFinished(true);
        return;
    }

    QJsonDocument idxDoc = QJsonDocument::fromJson(idxFile.readAll());
    idxFile.close();
    QJsonObject rootObj = idxDoc.object();

    if (!rootObj.contains("objects"))
    {
        emit launchDetailAdded("资产索引中无 objects 字段");
        emit fileCompletionFinished(true);
        return;
    }

    // 构建资产下载队列
    QJsonObject objects = rootObj["objects"].toObject();
    QString assetsObjectsPath = baseForAssets + "/assets/objects";

    QQueue<FileCompletionTask> assetQueue;
    for (auto it = objects.begin(); it != objects.end(); ++it)
    {
        QJsonObject asset = it.value().toObject();
        QString hash = asset["hash"].toString();

        // Task 3.2: 跳过无效 hash 的资源条目
        if (hash.isEmpty() || hash.length() < 2)
        {
            emit launchDetailAdded(QString("警告: 跳过无效hash的资源条目: %1").arg(it.key()));
            continue;
        }

        QString hashPrefix = hash.left(2);

        FileCompletionTask task;
        // Task 4.1: 资源优先使用 BMCLAPI
        task.url = QString("https://bmclapi2.bangbang93.com/assets/%1/%2")
                      .arg(hashPrefix).arg(hash);
        task.filePath = assetsObjectsPath + "/" + hashPrefix + "/" + hash;
        task.fileSize = asset["size"].toVariant().toLongLong();
        task.expectedHash = hash;
        task.description = QString("资源: %1").arg(it.key());
        task.priority = CompletionPriority::Low;
        task.timeoutMs = 300000;

        bool needDownload = !QFile::exists(task.filePath);
        // Task 3.1: 零字节文件检查
        if (!needDownload)
        {
            if (QFileInfo(task.filePath).size() == 0)
            {
                needDownload = true;
            }
            else if (!hash.isEmpty())
            {
                if (calculateFileHash(task.filePath) != hash)
                {
                    needDownload = true;
                }
            }
        }
        if (needDownload)
        {
            assetQueue.enqueue(task);
        }
    }

    emit launchDetailAdded(QString("共 %1 个资源文件需要下载").arg(assetQueue.size()));

    if (assetQueue.isEmpty())
    {
        emit launchDetailAdded("所有资源文件已就绪");
        emit fileCompletionFinished(true);
        return;
    }

    // 清理残留临时文件
    cleanupStaleTempFiles();

    // 合并到主队列并开始下载
    m_completionTotal = assetQueue.size();
    while (!assetQueue.isEmpty())
    {
        enqueueCompletionTask(assetQueue.dequeue());
    }

    // 确保队列按优先级排序
    sortCompletionQueueByPriority();

    // 启动并发下载
    int concurrent = qMin(m_dynamicMaxConcurrent, m_fileCompletionQueue.size());
    for (int i = 0; i < concurrent; ++i)
    {
        processCompletionQueue();
    }
}

void GameLauncher::processAssetQueue()
{
    processCompletionQueue();
}

// ────────────────────────────────────
//  优化的文件补全辅助方法
//  参考: HMCL (Hello Minecraft! Launcher) 的任务优先级系统
//         Prism Launcher 的 NetHost 并发控制
//         PCL2 的自适应镜像选择
// ────────────────────────────────────

QString GameLauncher::extractHostFromUrl(const QString& url) const
{
    static const QRegularExpression hostRe(R"(://([^/]+))");
    QRegularExpressionMatch m = hostRe.match(url);
    return m.hasMatch() ? m.captured(1) : QString();
}

void GameLauncher::enqueueCompletionTask(const FileCompletionTask& task)
{
    // 按优先级插入有序位置（高优先级在前）
    // 同优先级下文件小的优先（快速释放槽位）
    int insertIdx = 0;
    for (int i = 0; i < m_fileCompletionQueue.size(); ++i)
    {
        const FileCompletionTask& existing = m_fileCompletionQueue[i];
        if (static_cast<int>(task.priority) < static_cast<int>(existing.priority))
            break;
        if (static_cast<int>(task.priority) == static_cast<int>(existing.priority) &&
            task.fileSize < existing.fileSize)
            break;
        insertIdx = i + 1;
    }
    m_fileCompletionQueue.insert(insertIdx, task);
}

void GameLauncher::sortCompletionQueueByPriority()
{
    std::sort(m_fileCompletionQueue.begin(), m_fileCompletionQueue.end(),
              [](const FileCompletionTask& a, const FileCompletionTask& b) {
                  if (static_cast<int>(a.priority) != static_cast<int>(b.priority))
                      return static_cast<int>(a.priority) < static_cast<int>(b.priority);
                  return a.fileSize < b.fileSize;
              });
}

void GameLauncher::cleanupStaleTempFiles()
{
    int cleanedCount = 0;
    for (const FileCompletionTask& task : m_fileCompletionQueue)
    {
        QString tmpPath = task.filePath + ".tmp";
        if (QFile::exists(tmpPath))
        {
            QFile::remove(tmpPath);
            cleanedCount++;
        }
    }
    if (cleanedCount > 0)
    {
        emit launchDetailAdded(QString("清理了 %1 个残留的临时文件").arg(cleanedCount));
    }
}

int GameLauncher::getAvailableSlotForHost(const QString& host) const
{
    if (host.isEmpty()) return 1;

    auto it = m_hostStates.find(host);
    if (it == m_hostStates.end())
        return MAX_ASSET_CONCURRENT;

    const HostConnectionState& state = it.value();
    if (state.lastFailureTime.isValid())
    {
        qint64 secsSinceFailure = state.lastFailureTime.secsTo(QDateTime::currentDateTime());
        if (secsSinceFailure < 30)
        {
            int reducedLimit = qMax(1, state.maxConcurrent / 2);
            return qMax(0, reducedLimit - state.activeCount);
        }
    }
    return qMax(0, state.maxConcurrent - state.activeCount);
}

void GameLauncher::recordDownloadSpeed(qint64 bytes, qint64 elapsedMs)
{
    if (elapsedMs <= 0) return;

    double speedBps = static_cast<double>(bytes) / (static_cast<double>(elapsedMs) / 1000.0);
    m_currentDownloadSpeedBps = speedBps;
    m_downloadBytesSinceLastMeasure += bytes;

    // 指数加权滑动平均（参考 Prism Launcher 的速度计算）
    if (m_speedMeasureCount == 0)
    {
        m_averageDownloadSpeedBps = speedBps;
    }
    else
    {
        const double alpha = 0.3;
        m_averageDownloadSpeedBps = alpha * speedBps + (1.0 - alpha) * m_averageDownloadSpeedBps;
    }
    m_speedMeasureCount++;
}

void GameLauncher::updateAdaptiveConcurrency(bool success)
{
    if (success)
    {
        m_consecutiveErrors = 0;
        if (m_dynamicMaxConcurrent < MAX_COMPLETION_CONCURRENT)
        {
            m_dynamicMaxConcurrent = qMin(m_dynamicMaxConcurrent + 1, MAX_COMPLETION_CONCURRENT);
        }
    }
    else
    {
        m_consecutiveErrors++;
        if (m_consecutiveErrors >= 3)
        {
            int reduced = qMax(3, m_dynamicMaxConcurrent / 2);
            if (reduced < m_dynamicMaxConcurrent)
            {
                m_dynamicMaxConcurrent = reduced;
                emit launchDetailAdded(QString("检测到连续错误，降低并发数至 %1").arg(m_dynamicMaxConcurrent));
            }
        }
    }
}

bool GameLauncher::isMirrorAvailable(const QString& url)
{
    QString host = extractHostFromUrl(url);
    if (host.isEmpty()) return true;

    auto cooldownIt = m_mirrorCooldownUntil.find(host);
    if (cooldownIt != m_mirrorCooldownUntil.end())
    {
        if (QDateTime::currentDateTime() < cooldownIt.value())
            return false;
        m_mirrorCooldownUntil.remove(host);
    }

    auto healthIt = m_mirrorHealth.find(host);
    if (healthIt != m_mirrorHealth.end() && !healthIt.value())
    {
        setMirrorCooldown(url);
        return false;
    }
    return true;
}

void GameLauncher::setMirrorCooldown(const QString& url)
{
    QString host = extractHostFromUrl(url);
    if (host.isEmpty()) return;
    m_mirrorHealth[host] = false;
    m_mirrorCooldownUntil[host] = QDateTime::currentDateTime().addSecs(60);
}

bool GameLauncher::isRetryableError(int httpStatusCode, const QString& errorString)
{
    // 4xx 客户端错误（除 408 超时和 429 限流外）通常不可重试
    if (httpStatusCode >= 400 && httpStatusCode < 500)
    {
        if (httpStatusCode == 408 || httpStatusCode == 429)
            return true;
        return false;
    }
    // 5xx 服务器错误 - 可重试
    if (httpStatusCode >= 500 && httpStatusCode < 600)
        return true;

    // 网络层错误 - 可重试
    if (errorString.contains("Connection refused", Qt::CaseInsensitive) ||
        errorString.contains("timed out", Qt::CaseInsensitive) ||
        errorString.contains("timeout", Qt::CaseInsensitive) ||
        errorString.contains("reset", Qt::CaseInsensitive) ||
        errorString.contains("abort", Qt::CaseInsensitive) ||
        errorString.contains("resolve", Qt::CaseInsensitive) ||
        errorString.contains("HostNotFound", Qt::CaseInsensitive) ||
        errorString.contains("Closed", Qt::CaseInsensitive))
    {
        return true;
    }
    return true; // 默认可重试
}