#include "ForgeInstaller.h"
#include "ForgeDownloader.h"
#include "ForgeNewInstallTask.h"
#include "ForgeProfile.h"
#include "LegacyForgeInstaller.h"

#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QProcessEnvironment>
#include <QtConcurrent/QtConcurrent>

#include "../SettingsManager.h"
#include "../DownloadTaskManager.h"
#include "../Safety.h"

ForgeInstaller *ForgeInstaller::m_instance = nullptr;

// 兼容两种调用方传入的路径：<root>/versions/<versionId> 形式的版本目录，
// 或者直接是包含 versions/、libraries/ 的游戏根目录
static QString resolveMinecraftRoot(const QString &instancePath)
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

// 读取版本目录里的版本 JSON（<目录名>/<目录名>.json，回退 version.json），失败返回空对象
static QJsonObject readVersionJsonFromDir(const QString &versionDir)
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

// 官方老安装器自己决定版本目录名（新老格式不一），安装刚结束时
// 扫描 versions/ 找出它新建的 Forge 目录：名称含 forge、关联当前原版、JSON 最新
static QString findInstallerCreatedVersionId(const QString &gameRoot, const QString &mcVersion)
{
    const QDir versionsDir(gameRoot + "/versions");
    QString best;
    QDateTime bestTime;
    for (const QFileInfo &sub :
         versionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (sub.fileName() == mcVersion)
            continue;
        const QString jsonPath = sub.absoluteFilePath() + "/" + sub.fileName() + ".json";
        const QFileInfo jsonInfo(jsonPath);
        if (!jsonInfo.exists() || !sub.fileName().contains(QLatin1String("forge"), Qt::CaseInsensitive))
            continue;
        const QJsonObject json = readVersionJsonFromDir(sub.absoluteFilePath());
        if (json.isEmpty())
            continue;
        const bool related = json.value("inheritsFrom").toString() == mcVersion ||
                             sub.fileName().startsWith(mcVersion);
        if (!related)
            continue;
        // 只认本次安装窗口内（10 分钟超时 + 余量）新建/改写的目录，避免误收旧实例
        if (jsonInfo.lastModified() > QDateTime::currentDateTime().addSecs(-16 * 60) &&
            (best.isEmpty() || jsonInfo.lastModified() > bestTime))
        {
            best = sub.fileName();
            bestTime = jsonInfo.lastModified();
        }
    }
    return best;
}

// 把原版基准目录合进 Forge 版本目录，让实例自包含、versions/ 下只留一个文件夹：
// 1) 原版 JSON 按 mergeInheritsFromJson 的同套规则并进 Forge JSON，去掉 inheritsFrom；
// 2) 老安装器不往实例目录放客户端 jar 的，补拷一份 <mc>.jar；
// 3) 原版目录仅在「没有其他实例仍在继承」且「目录里没有存档/mod 等用户数据」时回收。
// 任一前置条件不满足就保持原状（两份目录照旧能跑），不影响安装成败。
static void consolidateForgeVersionDir(const QString &gameRoot, const QString &mcVersion,
                                       const QString &versionId, bool copyVanillaJar)
{
    const QString versionsDirPath = gameRoot + "/versions";
    const QString childDir = versionsDirPath + "/" + versionId;
    const QString parentDir = versionsDirPath + "/" + mcVersion;

    const QJsonObject child = readVersionJsonFromDir(childDir);
    if (child.isEmpty() || child.value("inheritsFrom").toString() != mcVersion)
        return; // 没有 inheritsFrom：实例本就不依赖原版目录，无需合并
    const QJsonObject parent = readVersionJsonFromDir(parentDir);
    if (parent.isEmpty())
        return; // 原版 JSON 缺失：保留两份，实例还能靠原版目录启动

    QJsonObject merged = child;

    // 老安装器路径没有客户端 jar：去掉继承后必须有本体才能启动
    if (copyVanillaJar && !QFile::exists(childDir + "/" + mcVersion + ".jar"))
    {
        const QString parentJar = parentDir + "/" + mcVersion + ".jar";
        if (QFile::exists(parentJar))
            QFile::copy(parentJar, childDir + "/" + mcVersion + ".jar");
    }

    // libraries：原版在前、Forge 在后（与启动时 mergeInheritsFromJson 的顺序一致）
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
        return;
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
            return;
    }
    // 原版目录里出现非标准文件（版本隔离产生的存档/设置/mod 等用户数据）→ 保留
    const QSet<QString> allowed{mcVersion + ".jar", mcVersion + ".json", QStringLiteral("version.json"),
                                 mcVersion + "-natives", QStringLiteral(".merge-tmp.jar")};
    for (const QFileInfo &entry : QDir(parentDir).entryInfoList(
             QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
    {
        if (!allowed.contains(entry.fileName()))
            return;
    }
    QDir(parentDir).removeRecursively();
}

ForgeInstaller* ForgeInstaller::instance()
{
    if (!m_instance) {
        m_instance = new ForgeInstaller();
    }
    return m_instance;
}

ForgeInstaller::ForgeInstaller(QObject *parent)
    : QObject(parent)
    , m_downloader(new forge::ForgeDownloader(this))
    , m_installTask(nullptr)
    , m_isInstalling(false)
    , m_versionIsolationEnabled(false)
{
    setDownloadSource(SettingsManager::instance()->getForgeDownloadSource());
}

void ForgeInstaller::setDownloadSource(ForgeDownloadSource source)
{
    m_downloader->setDownloadSource(
        source == ForgeDownloadSource::Official ? forge::DownloadSource::Official : forge::DownloadSource::BMCL);
}

ForgeInstaller::~ForgeInstaller()
{
    cancelInstall();

    if (!m_tempDir.isEmpty())
    {
        QDir tempDir(m_tempDir);
        if (tempDir.exists())
        {
            tempDir.removeRecursively();
        }
    }
}

void ForgeInstaller::setCurrentTaskId(const QString &taskId)
{
    m_taskId = taskId;
}

void ForgeInstaller::downloadForgeInstaller(const QString &mcVersion, const QString &forgeVersion,
                                              const QString &instancePath, const QString &instanceName)
{
    Q_UNUSED(instanceName);
    m_mcVersion = mcVersion;
    m_forgeVersion = forgeVersion;
    m_instancePath = instancePath;
    startInstall();
}

void ForgeInstaller::setInstancePath(const QString &path)
{
    m_instancePath = path;
}

void ForgeInstaller::setMcVersion(const QString &version)
{
    m_mcVersion = version;
}

void ForgeInstaller::setForgeVersion(const QString &version)
{
    m_forgeVersion = version;
}

void ForgeInstaller::setTaskId(const QString &taskId)
{
    m_taskId = taskId;
}

QString ForgeInstaller::taskId() const
{
    return m_taskId;
}

void ForgeInstaller::setVersionIsolationEnabled(bool enabled)
{
    m_versionIsolationEnabled = enabled;
}

void ForgeInstaller::startInstall()
{
    if (m_isInstalling)
    {
        emit installFailed(m_taskId, tr("安装器正忙"));
        return;
    }

    m_isInstalling = true;
    m_installerAttempt = 0;
    // MC ≤1.5.2 没有（可用的）无头安装器：下载 universal/client 压缩包走 MultiMC Legacy 合并路径
    m_legacyMode = legacyforge::isLegacyMcVersion(m_mcVersion);
    m_legacySettled = false;

    m_tempDir = QDir::tempPath() + "/BlockBox_Forge_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir tempDir(m_tempDir);
    if (!tempDir.exists())
    {
        tempDir.mkpath(".");
    }

    m_installerJarPath = m_tempDir + (m_legacyMode ? "/forge-universal.zip" : "/forge-installer.jar");

    emit installStarted(m_taskId);
    attemptInstallerDownload();
}

void ForgeInstaller::attemptInstallerDownload()
{
    m_installerAttemptSettled = false;

    // Legacy 模式下载 universal/client 压缩包，否则下载安装器 JAR
    const QStringList candidateUrls =
        m_legacyMode
            ? legacyforge::forgeZipCandidateUrls(
                  m_mcVersion, m_forgeVersion,
                  m_downloader->downloadSource() == forge::DownloadSource::BMCL)
            : m_downloader->getInstallerCandidateUrls(m_mcVersion, m_forgeVersion);
    QStringList candidateList = candidateUrls;
    // 换源重试：把另一组源排到最前（BMCL 与官方的安装器地址格式不同，都在候选里）
    if (m_installerAttempt > 0 && candidateList.size() > 1)
    {
        candidateList.move(1, 0);
    }

    const QString stageLabel = m_legacyMode ? tr("正在下载Forge通用组件包") : tr("正在下载Forge安装包");
    emit installProgress(m_taskId, stageLabel, 0);
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          stageLabel);
        // 参考 HMCL：不重置进度，Forge 阶段从 60% 起步
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 60);

    QMetaObject::Connection progressConn = connect(m_downloader, &forge::ForgeDownloader::downloadBytesProgress, this,
        [this](qint64 bytesReceived, qint64 bytesTotal)
        {
            if (!m_taskId.isEmpty())
            {
                DownloadTaskManager::instance()->updateTaskFileProgress(
                    m_taskId, "forge-installer.jar", bytesReceived, bytesTotal);
            }
        });

    QMetaObject::Connection percentConn = connect(m_downloader, &forge::ForgeDownloader::downloadProgress, this,
        [this](const QString &fileName, int percent)
        {
            Q_UNUSED(fileName);
            if (!m_taskId.isEmpty())
            {
                // 安装器 JAR 下载进度 → 映射到总体百分比 60-90%
                DownloadTaskManager::instance()->updateTaskProgressPercent(
                    m_taskId, 60 + percent * 30 / 100);
            }
        });

    // 下载超时看门狗（参考 HMCL 做法：单源 2 分钟整体超时）
    QTimer *downloadTimeout = new QTimer(this);
    downloadTimeout->setSingleShot(true);
    connect(downloadTimeout, &QTimer::timeout, this, [this, downloadTimeout]()
    {
        qWarning() << "[ForgeInstaller] 安装器JAR下载超时（2分钟），换源重试或取消下载";
        m_downloader->cancelAll();
        downloadTimeout->deleteLater();
        // 超时先当失败走重试链：还有换源机会就再试，否则报错
        onInstallerDownloadAttemptDone(false);
    });
    downloadTimeout->start(2 * 60 * 1000);

    m_downloader->downloadFile(candidateUrls, m_installerJarPath,
                              [this, progressConn, percentConn, downloadTimeout](bool success)
                              {
                                  disconnect(progressConn);
                                  disconnect(percentConn);
                                  downloadTimeout->stop();
                                  downloadTimeout->deleteLater();
                                  onInstallerDownloadAttemptDone(success);
                              });
}

void ForgeInstaller::onInstallerDownloadAttemptDone(bool success)
{
    // 看门狗 abort 与下载回调可能先后各触发一次，本轮只结算一次
    if (m_installerAttemptSettled)
    {
        return;
    }
    m_installerAttemptSettled = true;

    if (success)
    {
        onInstallerDownloaded(true);
        return;
    }

    // 一个源失败/超时：自动换另一个源重试一次（镜像偶发挂起是常态）
    if (m_isInstalling && m_installerAttempt == 0)
    {
        ++m_installerAttempt;
        qDebug() << "[ForgeInstaller] 安装器下载失败，切换下载源重试";
        DownloadTaskManager::instance()->updateTaskStatus(
            m_taskId, DownloadTaskStatus::Downloading, tr("下载源无响应，正在切换镜像重试"));
        attemptInstallerDownload();
        return;
    }

    onInstallerDownloaded(false);
}

void ForgeInstaller::onInstallerDownloaded(bool success)
{
    if (!success)
    {
        m_isInstalling = false;
        emit installFailed(m_taskId, tr("下载Forge安装包失败"));
        return;
    }

    DownloadTaskManager::instance()->updateTaskStageProgress(m_taskId, DownloadStage::LoaderInstall, 0);
    DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                      tr("正在识别Forge安装器类型"));

    // Legacy 模式没有安装器可识别，直接进入合并流程
    if (m_legacyMode)
    {
        startLegacyInstallTask();
        return;
    }

    // 先识别安装器类型：新版（含 spec 字段）走 MultiMC 式解析安装，旧版走官方安装器
    detectInstallerTypeAsync(m_installerJarPath);
}

void ForgeInstaller::onInstallerTypeDetected(int installerType)
{
    if (installerType == 0)
    {
        DownloadTaskManager::instance()->updateTaskStageProgress(m_taskId, DownloadStage::LoaderInstall, 0);
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在安装Forge"));
        startNewInstallTask();
    }
    else if (installerType == 1)
    {
        // 旧版（1.12 及以前）安装器没有处理器流程，仍交给官方安装器处理
        DownloadTaskManager::instance()->updateTaskStageProgress(m_taskId, DownloadStage::LoaderInstall, 0);
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在安装Forge"));
        runInstallerDirectly();
    }
    else
    {
        m_isInstalling = false;
        emit installFailed(m_taskId, tr("无法识别Forge安装器类型"));
    }
}

void ForgeInstaller::startNewInstallTask()
{
    // 安装进度统一由 ForgeInstaller 映射到任务系统（60%→100%），
    // 任务自身不直写 DownloadTaskManager
    QString minecraftRoot = resolveMinecraftRoot(m_instancePath);

    if (m_installTask)
    {
        m_installTask->deleteLater();
        m_installTask = nullptr;
    }
    m_installTask = new forge::ForgeNewInstallTask(m_installerJarPath, minecraftRoot, this);

    connect(m_installTask, &forge::ForgeNewInstallTask::progressUpdated, this,
            [this](const QString &stage, int percent)
            {
                emit installProgress(m_taskId, stage, percent);
                if (!m_taskId.isEmpty())
                {
                    DownloadTaskManager::instance()->updateTaskStatus(
                        m_taskId, DownloadTaskStatus::Downloading, stage);
                    DownloadTaskManager::instance()->updateTaskProgressPercent(
                        m_taskId, 60 + percent * 40 / 100);
                }
            });
    connect(m_installTask, &forge::ForgeNewInstallTask::completed, this,
            [this](bool success, const QString &error)
            {
                onNewInstallCompleted(success, error);
            });

    m_installTask->execute();
}

void ForgeInstaller::startLegacyInstallTask()
{
    // MultiMC Legacy 路径（MC ≤1.5.2）：universal 包合并进原版 jar + FML 依赖库
    DownloadTaskManager::instance()->updateTaskStageProgress(m_taskId, DownloadStage::LoaderInstall, 0);
    DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                      tr("正在安装Forge（经典模式）"));
    emit installProgress(m_taskId, tr("正在合并Forge到游戏文件"), 70);
    if (!m_taskId.isEmpty())
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 70);

    const QString minecraftRoot = resolveMinecraftRoot(m_instancePath);
    const QString vanillaJar = minecraftRoot + "/versions/" + m_mcVersion + "/" + m_mcVersion + ".jar";
    if (!QFile::exists(vanillaJar))
    {
        finishLegacyInstall(tr("未找到 Minecraft %1 的原版文件，请先下载原版再安装Forge").arg(m_mcVersion));
        return;
    }

    // 子版本目录 versions/<mc>-forge-<forge>；合并 jar 命名为 <mc>.jar，
    // 这样启动器经 inheritsFrom 解析出的 jar 名能在实例目录里直接命中
    const QString versionId = m_mcVersion + "-forge-" + m_forgeVersion;
    const QString versionDir = minecraftRoot + "/versions/" + versionId;
    QDir().mkpath(versionDir);
    // 合并产物直接写进目标目录（临时目录与游戏根可能不在同一块盘，
    // Windows 上跨盘 rename 会失败；同目录改名则没有这个问题）
    const QString mergedTemp = versionDir + "/.merge-tmp.jar";
    QFile::remove(mergedTemp);
    const QString outJar = versionDir + "/" + m_mcVersion + ".jar";

    const QString forgeZip = m_installerJarPath;
    (void)QtConcurrent::run([this, forgeZip, vanillaJar, mergedTemp, versionDir, versionId, outJar, minecraftRoot]() {
        QString mergeError;
        const bool ok = legacyforge::mergeForgeIntoJar(forgeZip, vanillaJar, mergedTemp, &mergeError);
        QMetaObject::invokeMethod(this, [this, ok, mergeError, mergedTemp, versionDir, versionId, outJar, minecraftRoot]() {
            if (m_legacySettled || !m_isInstalling)
                return; // 已取消：残留的 .merge-tmp.jar 留给下次安装覆盖
            if (!ok)
            {
                finishLegacyInstall(tr("合并Forge文件失败：") + mergeError);
                return;
            }

            QFile::remove(outJar);
            if (!QFile::rename(mergedTemp, outJar))
            {
                finishLegacyInstall(tr("无法写入合并后的游戏文件：%1").arg(outJar));
                return;
            }

            // 子版本 JSON：inheritsFrom 指回原版，FML 库挂进 libraries
            const QList<legacyforge::FmlLib> fmlLibs = legacyforge::fmlLibsFor(m_mcVersion);
            const QString jsonPath = versionDir + "/" + versionId + ".json";
            QFile jsonFile(jsonPath);
            if (!jsonFile.open(QIODevice::WriteOnly))
            {
                finishLegacyInstall(tr("无法写入版本配置文件：%1").arg(jsonPath));
                return;
            }
            jsonFile.write(QJsonDocument(
                               legacyforge::buildLegacyVersionJson(m_mcVersion, m_forgeVersion, fmlLibs))
                               .toJson());
            jsonFile.close();

            // FML 依赖库（MultiMC fmlLibsMapping）：已存在的跳过，缺的批量补齐
            const QString libsDir = minecraftRoot + "/libraries/fmllibs";
            QDir().mkpath(libsDir);
            QVector<forge::ForgeFileDownload> libTasks;
            for (const legacyforge::FmlLib &lib : fmlLibs)
            {
                const QString target = libsDir + "/" + lib.filename;
                if (QFile::exists(target))
                    continue;
                libTasks.append({legacyforge::fmlLibCandidateUrls(lib), target, lib.sha1});
            }
            if (libTasks.isEmpty())
            {
                finishLegacyInstall();
                return;
            }

            emit installProgress(m_taskId, tr("正在下载Forge依赖库"), 85);
            DownloadTaskManager::instance()->updateTaskStatus(
                m_taskId, DownloadTaskStatus::Downloading, tr("正在下载Forge依赖库"));
            m_downloader->downloadFiles(
                libTasks,
                [this](int completed, int total) {
                    if (m_taskId.isEmpty() || total <= 0)
                        return;
                    DownloadTaskManager::instance()->updateTaskProgressPercent(
                        m_taskId, 85 + qRound(qreal(completed) * 10 / total));
                },
                [this](bool allSuccess, const QStringList &failed) {
                    if (m_legacySettled || !m_isInstalling)
                        return;
                    if (!allSuccess)
                        finishLegacyInstall(tr("下载Forge依赖库（FML）失败：") + failed.join(QStringLiteral(", ")));
                    else
                        finishLegacyInstall();
                });
        });
    });
}

void ForgeInstaller::finishLegacyInstall(const QString &error)
{
    if (m_legacySettled)
        return;
    m_legacySettled = true;
    m_isInstalling = false;

    if (!m_tempDir.isEmpty())
    {
        QDir tempDir(m_tempDir);
        if (tempDir.exists())
            tempDir.removeRecursively();
        m_tempDir.clear();
    }

    if (!error.isEmpty())
    {
        emit installFailed(m_taskId, error);
        return;
    }

    // 原版基准目录并进 Legacy Forge 目录，versions/ 下只留一个文件夹
    consolidateForgeVersionDir(resolveMinecraftRoot(m_instancePath), m_mcVersion,
                               m_mcVersion + "-forge-" + m_forgeVersion, false);

    emit installProgress(m_taskId, tr("Forge安装完成"), 100);
    DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Completed,
                                                      tr("Forge安装完成"));
    DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 100);
    emit installCompleted(m_taskId);
}

void ForgeInstaller::runInstallerDirectly()
{
    // 安装器初始化 → 从 91% 开始
    DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 91);

    QString javaPath = getJavaPath();
    if (javaPath.isEmpty())
    {
        emit installFailed(m_taskId, tr("未找到Java运行环境"));
        m_isInstalling = false;
        return;
    }

    // Forge installer expects the Minecraft directory (containing versions/ and libraries/)
    QString minecraftDir = resolveMinecraftRoot(m_instancePath);

    // 验证 Minecraft 目录结构
    QDir versionsDir(minecraftDir + "/versions");
    if (!versionsDir.exists())
    {
        emit installFailed(m_taskId, tr("Minecraft目录结构不完整，请先下载原版"));
        m_isInstalling = false;
        return;
    }

    QProcess *process = new QProcess(this);
    process->setWorkingDirectory(minecraftDir);
    process->setProcessChannelMode(QProcess::MergedChannels);

    // 进度计时器：每 8 秒 +1%，从 91% 到 99%
    auto installProgressPercent = std::make_shared<int>(91);
    auto progressTimer = new QTimer(this);
    progressTimer->setInterval(8000);
    connect(progressTimer, &QTimer::timeout, this, [this, installProgressPercent]()
    {
        if (*installProgressPercent < 99)
        {
            (*installProgressPercent)++;
            DownloadTaskManager::instance()->updateTaskProgressPercent(
                m_taskId, *installProgressPercent);
        }
    });

    // 10 分钟超时保护
    auto timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [this, process, progressTimer]()
    {
        progressTimer->stop();
        process->kill();
        process->deleteLater();
        emit installFailed(m_taskId, tr("Forge安装超时（超过10分钟），请检查网络连接"));
    });
    timeoutTimer->start(10 * 60 * 1000);

    // 共用进度解析逻辑（stdout 和 stderr 合并为 MergedChannels）
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process, installProgressPercent]()
    {
        QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        if (output.isEmpty()) return;

        const QStringList progressKeywords = {
            "Considering library", "Downloading library", "Extracting",
            "Processing", "Installing", "Maven", "MinecraftForge",
            "Installer", "library", "Downloaded"
        };

        bool matched = false;
        for (const QString &kw : progressKeywords)
        {
            if (output.contains(kw, Qt::CaseInsensitive))
            {
                matched = true;
                break;
            }
        }

        if (matched)
        {
            *installProgressPercent = qMin(99, *installProgressPercent + 2);
        }

        emit installProgress(m_taskId, output, *installProgressPercent);
        DownloadTaskManager::instance()->updateTaskProgressPercent(
            m_taskId, *installProgressPercent);
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, progressTimer, timeoutTimer, minecraftDir](int exitCode, QProcess::ExitStatus exitStatus)
    {
        progressTimer->stop();
        timeoutTimer->stop();
        process->deleteLater();

        if (exitStatus == QProcess::NormalExit && exitCode == 0)
        {
            // 原版基准目录并进安装器生成的 Forge 目录，versions/ 下只留一个文件夹
            const QString createdVersionId = findInstallerCreatedVersionId(minecraftDir, m_mcVersion);
            if (!createdVersionId.isEmpty())
                consolidateForgeVersionDir(minecraftDir, m_mcVersion, createdVersionId, true);
            emit installProgress(m_taskId, tr("Forge安装完成"), 100);
            DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Completed,
                                                              tr("Forge安装完成"));
            DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 100);
            emit installCompleted(m_taskId);
        }
        else
        {
            QString errorOutput = QString::fromUtf8(process->readAllStandardOutput());
            QString errorMsg = tr("Forge安装失败 (退出码: %1)").arg(exitCode);
            if (!errorOutput.isEmpty())
            {
                errorMsg += "\n" + errorOutput.left(500);
            }
            emit installFailed(m_taskId, errorMsg);
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, progressTimer, timeoutTimer](QProcess::ProcessError error)
    {
        Q_UNUSED(error);
        progressTimer->stop();
        timeoutTimer->stop();
        process->deleteLater();
        emit installFailed(m_taskId, tr("无法启动Forge安装器"));
    });

    // JVM 参数参考 HMCL/PCL 标准做法
    QStringList args;
    args << "-Djava.net.preferIPv4Stack=true"
         << "-jar" << m_installerJarPath
         << "--installClient" << minecraftDir;

    qDebug() << "[ForgeInstaller]" << "Starting:" << javaPath << args;

    process->start(javaPath, args);
    progressTimer->start();
}

void ForgeInstaller::onNewInstallCompleted(bool success, const QString &error)
{
    m_isInstalling = false;

    // 任务对象与其临时目录在这里统一回收：
    // 工作线程（解压/处理器）此时已结束，删安装包是安全的
    const QString writtenVersionId = m_installTask ? m_installTask->writtenVersionId() : QString();
    if (m_installTask)
    {
        m_installTask->deleteLater();
        m_installTask = nullptr;
    }

    if (!m_tempDir.isEmpty())
    {
        QDir tempDir(m_tempDir);
        if (tempDir.exists())
        {
            tempDir.removeRecursively();
        }
        m_tempDir.clear();
    }

    if (success)
    {
        // 原版基准目录并进生成的 Forge 目录，versions/ 下只留一个文件夹
        if (!writtenVersionId.isEmpty())
            consolidateForgeVersionDir(resolveMinecraftRoot(m_instancePath), m_mcVersion,
                                       writtenVersionId, false);
        emit installProgress(m_taskId, tr("Forge安装完成"), 100);
            DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Completed,
                                                              tr("Forge安装完成"));
            DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 100);
            emit installCompleted(m_taskId);
    }
    else
    {
        emit installFailed(m_taskId, error);
    }
}

QString ForgeInstaller::getJavaPath()
{
    QString javaPath = "java";

    if (QFile::exists(javaPath))
    {
        return javaPath;
    }

    QString javaHome = qEnvironmentVariable("JAVA_HOME");
    if (!javaHome.isEmpty())
    {
        QString javaExe = javaHome + "/bin/java";
#ifdef Q_OS_WIN
        javaExe += ".exe";
#endif
        if (QFile::exists(javaExe))
        {
            return javaExe;
        }
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");
    const QStringList paths = path.split(QDir::listSeparator());

    for (const QString &p : paths)
    {
        QString javaExe = p + "/java";
#ifdef Q_OS_WIN
        javaExe += ".exe";
#endif
        if (QFile::exists(javaExe))
        {
            return javaExe;
        }
    }

    return QString();
}

QString ForgeInstaller::getJarToolPath()
{
    QString javaPath = SettingsManager::instance()->getJavaPath();

    // Java 设置为「自动选择」时存的是字面量 auto，不是可执行文件路径，
    // 直接把 java 替换成 jar 会得到 "auto"，必须先换成真实可用的查找方式
    if (javaPath.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0)
    {
        javaPath.clear();
    }

    if (javaPath.isEmpty() || !QFile::exists(javaPath))
    {
        QString found = QStandardPaths::findExecutable(QStringLiteral("jar"));
        if (!found.isEmpty())
        {
            return found;
        }

        javaPath = qEnvironmentVariable("JAVA_HOME");
        if (!javaPath.isEmpty())
        {
#ifdef Q_OS_WIN
            javaPath += "/bin/jar.exe";
#else
            javaPath += "/bin/jar";
#endif
            if (QFile::exists(javaPath))
            {
                return javaPath;
            }
        }
#ifdef Q_OS_WIN
        return QStringLiteral("jar.exe");
#else
        return QStringLiteral("jar");
#endif
    }

#ifdef Q_OS_WIN
    javaPath.replace("java.exe", "jar.exe");
    javaPath.replace("javaw.exe", "jar.exe");
#else
    javaPath.replace("java", "jar");
#endif

    return javaPath;
}

QByteArray ForgeInstaller::decompressRawDeflate(const QByteArray &compressedData, quint32 uncompressedSize)
{
    // Qt qUncompress() requires the data to be in zlib format with
    // a 4-byte big-endian uncompressed size header at the beginning.
    // The input is raw deflate data, so we need to prepend the header.
    //
    // Reference: https://doc.qt.io/qt-6/qbytearray.html#qUncompress
    QByteArray input;
    input.append(static_cast<char>((uncompressedSize >> 24) & 0xFF));
    input.append(static_cast<char>((uncompressedSize >> 16) & 0xFF));
    input.append(static_cast<char>((uncompressedSize >> 8) & 0xFF));
    input.append(static_cast<char>(uncompressedSize & 0xFF));
    input.append(compressedData);

    return qUncompress(input);
}

bool ForgeInstaller::extractEntryFromJar(const QString &jarPath, const QString &entryPath,
                                         const QString &destPath)
{
    if (!Safety::isSafeEntryName(entryPath))
    {
        qWarning() << "[ForgeInstaller]" << "Unsafe entry path rejected:" << entryPath;
        return false;
    }

    // 进程内解析优先（毫秒级）：jar.exe/PowerShell 等外部工具在部分机器上
    // 启动极慢甚至卡到超时（实测出现过 40 秒以上的等待），只作兜底
    if (tryExtractManual(jarPath, entryPath, destPath))
    {
        return true;
    }

    if (tryExtractWithJarTool(jarPath, entryPath, destPath))
    {
        return true;
    }

#ifdef Q_OS_WIN
    if (tryExtractWithPowerShell(jarPath, entryPath, destPath))
    {
        return true;
    }
#else
    if (tryExtractWithUnzip(jarPath, entryPath, destPath))
    {
        return true;
    }
#endif

    if (tryExtractWithPython(jarPath, entryPath, destPath))
    {
        return true;
    }

    if (tryExtractManual(jarPath, entryPath, destPath))
    {
        return true;
    }

    qWarning() << "[ForgeInstaller]" << "All extraction methods failed for"
               << "jar:" << jarPath << "entry:" << entryPath;
    return false;
}

bool ForgeInstaller::tryExtractWithJarTool(const QString &jarPath, const QString &entryPath,
                                           const QString &destPath)
{
    QString jarToolPath = getJarToolPath();
    if (jarToolPath.isEmpty())
    {
        qWarning() << "[ForgeInstaller] jar.exe not found (JDK required)";
        return false;
    }

    // 绝对路径不存在时直接放弃，避免 QProcess 起一个必然失败的进程干等超时
    if (jarToolPath.contains('/') && !QFile::exists(jarToolPath))
    {
        return false;
    }

    QProcess process;
    QStringList args;
    args << "-xf" << jarPath << entryPath;
    process.setWorkingDirectory(QFileInfo(destPath).absolutePath());
    process.start(jarToolPath, args);

    if (!process.waitForFinished(30000))
    {
        qWarning() << "[ForgeInstaller] jar.exe timed out";
        return false;
    }

    if (process.exitCode() != 0)
    {
        qWarning() << "[ForgeInstaller] jar.exe failed, exit code:" << process.exitCode();
        return false;
    }

    QString extractedPath = QFileInfo(destPath).absolutePath() + "/" + entryPath;
    if (!QFile::exists(extractedPath))
    {
        return false;
    }

    if (extractedPath != destPath)
    {
        QFile::remove(destPath);
        QFile::rename(extractedPath, destPath);
    }

    return true;
}

bool ForgeInstaller::tryExtractWithPowerShell(const QString &jarPath, const QString &entryPath,
                                              const QString &destPath)
{
    QString script = QString(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$zip = [System.IO.Compression.ZipFile]::OpenRead('%1');"
        "$entry = $zip.GetEntry('%2');"
        "if ($entry -eq $null) { exit 1; }"
        "$stream = $entry.Open();"
        "$destDir = Split-Path '%3' -Parent;"
        "if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null; }"
        "$fs = [System.IO.File]::Create('%3');"
        "$stream.CopyTo($fs);"
        "$fs.Close();"
        "$stream.Close();"
        "$zip.Dispose();"
    ).arg(Safety::psQuote(jarPath), Safety::psQuote(entryPath), Safety::psQuote(destPath));

    QProcess process;
    process.start("powershell", QStringList() << "-NoProfile" << "-Command" << script);

    if (!process.waitForFinished(30000))
    {
        qWarning() << "[ForgeInstaller] PowerShell timed out";
        return false;
    }

    if (process.exitCode() != 0)
    {
        qWarning() << "[ForgeInstaller] PowerShell failed, exit code:" << process.exitCode();
        return false;
    }

    if (!QFile::exists(destPath))
    {
        qWarning() << "[ForgeInstaller] PowerShell extraction succeeded but output file not found";
        return false;
    }

    return true;
}

bool ForgeInstaller::tryExtractWithPython(const QString &jarPath, const QString &entryPath,
                                          const QString &destPath)
{
    QStringList pythonCommands = {"python3", "python"};

    for (const QString &pythonCmd : pythonCommands)
    {
        // 路径走 argv 传入，避免含引号的文件名被拼进脚本造成命令注入
        const QString script = QStringLiteral(
            "import zipfile, sys, os\n"
            "src_zip, entry, dest = sys.argv[1], sys.argv[2], sys.argv[3]\n"
            "dest_dir = os.path.dirname(dest) or '.'\n"
            "os.makedirs(dest_dir, exist_ok=True)\n"
            "with zipfile.ZipFile(src_zip) as z:\n"
            "    z.extract(entry, dest_dir)\n"
            "tmp = os.path.join(dest_dir, entry)\n"
            "if os.path.abspath(tmp) != os.path.abspath(dest):\n"
            "    os.replace(tmp, dest)\n");

        QProcess process;
        process.start(pythonCmd, QStringList() << "-c" << script << jarPath << entryPath << destPath);

        if (!process.waitForFinished(15000))
        {
            qWarning() << "[ForgeInstaller] Python timed out:" << pythonCmd;
            continue;
        }

        if (process.exitCode() != 0)
        {
            qWarning() << "[ForgeInstaller] Python failed:" << pythonCmd
                       << "exit code:" << process.exitCode();
            continue;
        }

        if (QFile::exists(destPath) && QFileInfo(destPath).size() > 0)
        {
            return true;
        }
    }

    qWarning() << "[ForgeInstaller] Python not available or extraction failed";
    return false;
}

bool ForgeInstaller::tryExtractWithUnzip(const QString &jarPath, const QString &entryPath,
                                         const QString &destPath)
{
    QProcess process;
    QStringList args;
    args << "-p" << jarPath << entryPath;
    process.setStandardOutputFile(destPath);
    process.start("unzip", args);

    if (!process.waitForFinished(30000))
    {
        return false;
    }

    if (process.exitCode() != 0)
    {
        return false;
    }

    return QFile::exists(destPath) && QFileInfo(destPath).size() > 0;
}

bool ForgeInstaller::tryExtractManual(const QString &jarPath, const QString &entryPath,
                                      const QString &destPath)
{
    QFile jarFile(jarPath);
    if (!jarFile.open(QIODevice::ReadOnly))
    {
        qWarning() << "[ForgeInstaller] Manual extraction: cannot open JAR file:" << jarPath;
        return false;
    }

    qint64 fileSize = jarFile.size();
    if (fileSize < 22)
    {
        qWarning() << "[ForgeInstaller] Manual extraction: JAR file too small:" << fileSize;
        jarFile.close();
        return false;
    }

    jarFile.seek(fileSize - 22);
    QByteArray eocdData = jarFile.read(22);
    if (eocdData.size() < 22)
    {
        jarFile.close();
        return false;
    }

    quint32 eocdSignature = (quint8)eocdData[0] | ((quint8)eocdData[1] << 8)
                          | ((quint8)eocdData[2] << 16) | ((quint8)eocdData[3] << 24);
    if (eocdSignature != 0x06054b50)
    {
        eocdData.clear();
        qint64 searchStart = qMax((qint64)0, fileSize - 65557);
        jarFile.seek(searchStart);
        QByteArray tail = jarFile.read(fileSize - searchStart);
        int foundPos = -1;
        for (int i = tail.size() - 22; i >= 0; --i)
        {
            if ((quint8)tail[i] == 0x50 && (quint8)tail[i + 1] == 0x4b
                && (quint8)tail[i + 2] == 0x05 && (quint8)tail[i + 3] == 0x06)
            {
                foundPos = i;
                break;
            }
        }
        if (foundPos < 0)
        {
            qWarning() << "[ForgeInstaller] Manual extraction: ZIP EOCD not found";
            jarFile.close();
            return false;
        }
        eocdData = tail.mid(foundPos, 22);
    }

    quint16 totalEntries = (quint8)eocdData[10] | ((quint8)eocdData[11] << 8);
    quint32 centralDirOffset = (quint8)eocdData[16] | ((quint8)eocdData[17] << 8)
                             | ((quint8)eocdData[18] << 16) | ((quint8)eocdData[19] << 24);

    jarFile.seek(centralDirOffset);

    quint32 targetLocalHeaderOffset = 0;
    quint16 targetCompressionMethod = 0;
    quint32 targetCompressedSize = 0;
    quint32 targetUncompressedSize = 0;
    bool found = false;

    for (int i = 0; i < totalEntries; ++i)
    {
        QByteArray header = jarFile.read(46);
        if (header.size() < 46)
        {
            jarFile.close();
            return false;
        }

        quint32 signature = (quint8)header[0] | ((quint8)header[1] << 8)
                          | ((quint8)header[2] << 16) | ((quint8)header[3] << 24);
        if (signature != 0x02014b50)
        {
            break;
        }

        quint16 compressionMethod = (quint8)header[10] | ((quint8)header[11] << 8);
        quint32 compressedSize = (quint8)header[20] | ((quint8)header[21] << 8)
                               | ((quint8)header[22] << 16) | ((quint8)header[23] << 24);
        quint32 uncompressedSize = (quint8)header[24] | ((quint8)header[25] << 8)
                                 | ((quint8)header[26] << 16) | ((quint8)header[27] << 24);
        quint16 fileNameLength = (quint8)header[28] | ((quint8)header[29] << 8);
        quint16 extraFieldLength = (quint8)header[30] | ((quint8)header[31] << 8);
        quint16 commentLength = (quint8)header[32] | ((quint8)header[33] << 8);
        quint32 localHeaderOffset = (quint8)header[42] | ((quint8)header[43] << 8)
                                  | ((quint8)header[44] << 16) | ((quint8)header[45] << 24);

        QByteArray fileNameData = jarFile.read(fileNameLength);
        if (fileNameData.size() < fileNameLength)
        {
            jarFile.close();
            return false;
        }

        QString entryName = QString::fromUtf8(fileNameData);
        jarFile.read(extraFieldLength + commentLength);

        if (entryName == entryPath)
        {
            targetLocalHeaderOffset = localHeaderOffset;
            targetCompressionMethod = compressionMethod;
            targetCompressedSize = compressedSize;
            targetUncompressedSize = uncompressedSize;
            found = true;
            break;
        }
    }

    if (!found)
    {
        qWarning() << "[ForgeInstaller] Manual extraction: entry not found in JAR:" << entryPath;
        jarFile.close();
        return false;
    }

    jarFile.seek(targetLocalHeaderOffset);
    QByteArray localHeader = jarFile.read(30);
    if (localHeader.size() < 30)
    {
        jarFile.close();
        return false;
    }

    quint16 localFileNameLength = (quint8)localHeader[26] | ((quint8)localHeader[27] << 8);
    quint16 localExtraLength = (quint8)localHeader[28] | ((quint8)localHeader[29] << 8);

    jarFile.read(localFileNameLength + localExtraLength);

    QByteArray compressedData = jarFile.read(targetCompressedSize);
    if (compressedData.size() < (int)targetCompressedSize)
    {
        jarFile.close();
        return false;
    }

    jarFile.close();

    QByteArray outputData;

    if (targetCompressionMethod == 0)
    {
        outputData = compressedData;
    }
    else if (targetCompressionMethod == 8)
    {
        outputData = decompressRawDeflate(compressedData, targetUncompressedSize);
        if (outputData.isEmpty())
        {
            qWarning() << "[ForgeInstaller]" << "Deflate decompression failed for" << entryPath;
            return false;
        }
    }
    else
    {
        return false;
    }

    QDir destDir = QFileInfo(destPath).absoluteDir();
    if (!destDir.exists())
    {
        destDir.mkpath(".");
    }

    QFile outFile(destPath);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        return false;
    }

    outFile.write(outputData);
    outFile.close();

    return true;
}

int ForgeInstaller::detectInstallerType(const QString &jarPath)
{
    QString extractedPath = m_tempDir + "/install_profile.json";
    if (!extractEntryFromJar(jarPath, "install_profile.json", extractedPath))
    {
        qWarning() << "[ForgeInstaller]" << "Failed to extract install_profile.json from installer jar";
        return -1;
    }

    QFile file(extractedPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "[ForgeInstaller]" << "Failed to open install_profile.json";
        return -1;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        qWarning() << "[ForgeInstaller]" << "Failed to parse install_profile.json:" << parseError.errorString();
        return -1;
    }

    QJsonObject root = doc.object();

    if (root.contains("spec"))
    {
        qDebug() << "[ForgeInstaller]" << "Detected new version Forge installer (has 'spec' field)";
        return 0;
    }

    if (root.contains("install") && root.contains("versionInfo"))
    {
        qDebug() << "[ForgeInstaller]" << "Detected old version Forge installer (has 'install' and 'versionInfo' fields)";
        return 1;
    }

    qWarning() << "[ForgeInstaller]" << "Unknown installer type";
    return -1;
}

void ForgeInstaller::detectInstallerTypeAsync(const QString &jarPath)
{
    // Run extraction and type detection in a worker thread to avoid
    // blocking the UI thread with QProcess::waitForFinished() calls.
    auto future = QtConcurrent::run([this, jarPath]()
    {
        int result = detectInstallerType(jarPath);

        // Post result back to main thread
        QMetaObject::invokeMethod(this, [this, result]()
        {
            onInstallerTypeDetected(result);
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(future);
}

void ForgeInstaller::cancelInstall()
{
    if (!m_isInstalling)
    {
        return;
    }

    m_downloader->cancelAll();

    if (m_installTask)
    {
        // 只打取消标记并中止下载；任务对象和 m_tempDir（含安装包）延迟到
        // onNewInstallCompleted 回收，避免工作线程还在读安装包时被删
        m_installTask->cancel();
    }
    else if (!m_tempDir.isEmpty())
    {
        QDir tempDir(m_tempDir);
        if (tempDir.exists())
        {
            tempDir.removeRecursively();
        }
        m_tempDir.clear();
    }

    m_isInstalling = false;

    emit installCancelled(m_taskId);
}

bool ForgeInstaller::isInstalling() const
{
    return m_isInstalling;
}