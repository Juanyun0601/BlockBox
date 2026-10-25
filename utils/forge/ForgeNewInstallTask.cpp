/**
 * @file   ForgeNewInstallTask.cpp
 * @brief  Forge新版本安装任务实现
 * @author BlockBox Team
 * @date   2026-06-01
 */

#include "ForgeNewInstallTask.h"
#include "ForgeProfile.h"
#include "ForgeDownloader.h"
#include "../JarUtils.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QUuid>
#include <QLibrary>

#ifdef Q_OS_WIN
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

#ifndef Z_OK
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_FINISH 4
#endif

#include "../SettingsManager.h"
#include "../DownloadTaskManager.h"
#include "../GameLauncher.h"

namespace forge {

ForgeNewInstallTask::ForgeNewInstallTask(const QString &installerJarPath, const QString &instancePath,
                                         QObject *parent)
    : QObject(parent)
    , m_installerJarPath(installerJarPath)
    , m_instancePath(instancePath)
    , m_cancelled(false)
    , m_processorTotal(0)
    , m_processorDone(0)
{
}

ForgeNewInstallTask::~ForgeNewInstallTask()
{
    cleanupTempFiles();
}

void ForgeNewInstallTask::execute()
{
    m_tempDir = QDir::tempPath() + "/BlockBox_ForgeTask_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir tempDir(m_tempDir);
    if (!tempDir.exists())
    {
        tempDir.mkpath(".");
    }

    preExecute();
}

void ForgeNewInstallTask::cancel()
{
    m_cancelled = true;
    if (m_libDownloader)
    {
        m_libDownloader->cancelAll();
    }
}

void ForgeNewInstallTask::setTaskId(const QString &taskId)
{
    m_taskId = taskId;
}

void ForgeNewInstallTask::preExecute()
{
    emit progressUpdated(tr("正在解析安装配置"), 0);
    if (!m_taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在解析安装配置"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 0);
    }

    // Run all blocking JAR extraction operations in a worker thread to avoid
    // freezing the UI. Each extraction method uses QProcess::waitForFinished()
    // which can block for up to 30-120 seconds per attempt.
    QString installerJarPath = m_installerJarPath;
    QString tempDir = m_tempDir;
    QString instancePath = m_instancePath;

    auto future = QtConcurrent::run([this, installerJarPath, tempDir, instancePath]()
    {
        // --- parseInstallProfile ---
        QString profilePath = tempDir + "/install_profile.json";
        if (!JarUtils::extractFromJar(installerJarPath, "install_profile.json", profilePath))
        {
            QMetaObject::invokeMethod(this, [this]()
            {
                emit completed(false, tr("无法提取install_profile.json"));
            }, Qt::QueuedConnection);
            return;
        }

        QFile file(profilePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            QMetaObject::invokeMethod(this, [this]()
            {
                emit completed(false, tr("无法读取install_profile.json"));
            }, Qt::QueuedConnection);
            return;
        }

        QByteArray data = file.readAll();
        file.close();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError)
        {
            QString errorMsg = tr("install_profile.json解析失败: %1").arg(parseError.errorString());
            QMetaObject::invokeMethod(this, [this, errorMsg]()
            {
                emit completed(false, errorMsg);
            }, Qt::QueuedConnection);
            return;
        }

        m_profile = ForgeInstallProfile::fromJson(doc.object());

        // Progress update: install profile parsed
        QMetaObject::invokeMethod(this, [this]()
        {
            emit progressUpdated(tr("正在解析安装配置"), 4);
        }, Qt::QueuedConnection);

        if (m_cancelled)
        {
            QMetaObject::invokeMethod(this, [this]()
            {
                emit completed(false, tr("安装已取消"));
            }, Qt::QueuedConnection);
            return;
        }

        // --- extractVersionJson ---
        QString jsonEntry = m_profile.json;
        if (jsonEntry.startsWith("/"))
        {
            jsonEntry = jsonEntry.mid(1);
        }

        if (!jsonEntry.isEmpty())
        {
            QString versionJsonPath = tempDir + "/version.json";
            if (JarUtils::extractFromJar(installerJarPath, jsonEntry, versionJsonPath))
            {
                QFile vFile(versionJsonPath);
                if (vFile.open(QIODevice::ReadOnly))
                {
                    QJsonDocument vDoc = QJsonDocument::fromJson(vFile.readAll());
                    vFile.close();
                    if (vDoc.isObject())
                    {
                        m_baseVersionJson = vDoc.object();
                    }
                }
            }
            else
            {
                qWarning() << "[ForgeNewInstallTask]" << "Failed to extract version.json from installer";
            }
        }

        // Progress update: version.json extracted
        QMetaObject::invokeMethod(this, [this]()
        {
            emit progressUpdated(tr("正在解析版本配置"), 7);
        }, Qt::QueuedConnection);

        if (m_cancelled)
        {
            QMetaObject::invokeMethod(this, [this]()
            {
                emit completed(false, tr("安装已取消"));
            }, Qt::QueuedConnection);
            return;
        }

        // --- copyEmbeddedLibraries ---
        QString librariesPath = instancePath + "/libraries";
        QDir librariesDir(librariesPath);
        if (!librariesDir.exists())
        {
            librariesDir.mkpath(".");
        }

        QString tempExtractDir = tempDir + "/maven_extract";
        QDir extractDir(tempExtractDir);
        if (extractDir.exists())
        {
            extractDir.removeRecursively();
        }
        extractDir.mkpath(".");

        if (!JarUtils::extractJarDirectory(installerJarPath, "maven", tempExtractDir))
        {
            qWarning() << "[ForgeNewInstallTask]" << "Failed to extract maven directory from installer";
            QMetaObject::invokeMethod(this, [this]()
            {
                emit completed(false, tr("无法提取内嵌库文件，请确保已安装JDK"));
            }, Qt::QueuedConnection);
            return;
        }

        QString extractedMavenPath = tempExtractDir + "/maven";
        QDir mavenDir(extractedMavenPath);
        if (mavenDir.exists())
        {
            QStringList jarFiles;
            QDirIterator dirIt(extractedMavenPath,
                            QStringList() << "*.jar",
                            QDir::Files,
                            QDirIterator::Subdirectories);
            while (dirIt.hasNext())
            {
                jarFiles.append(dirIt.next());
            }

            int copiedCount = 0;
            for (const QString &srcFilePath : jarFiles)
            {
                QString relativePath = srcFilePath.mid(extractedMavenPath.length() + 1);
                QString destFilePath = librariesPath + "/" + relativePath;

                QFileInfo destFileInfo(destFilePath);
                if (destFileInfo.exists())
                {
                    continue;
                }

                QDir destDir = destFileInfo.absoluteDir();
                if (!destDir.exists())
                {
                    destDir.mkpath(".");
                }

                if (QFile::copy(srcFilePath, destFilePath))
                {
                    copiedCount++;
                }
                else
                {
                    qWarning() << "[ForgeNewInstallTask]" << "Failed to copy library:" << relativePath;
                }
            }

            qDebug() << "[ForgeNewInstallTask]" << "Copied" << copiedCount << "embedded libraries";
        }
        else
        {
            qWarning() << "[ForgeNewInstallTask]" << "No maven directory found after extraction";
        }

        QDir(tempExtractDir).removeRecursively();

        if (m_cancelled)
        {
            QMetaObject::invokeMethod(this, [this]()
            {
                emit completed(false, tr("安装已取消"));
            }, Qt::QueuedConnection);
            return;
        }

        // All blocking operations done, continue on main thread
        QMetaObject::invokeMethod(this, [this]()
        {
            onPreExtractDone();
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(future);
}

void ForgeNewInstallTask::onPreExtractDone()
{
    emit progressUpdated(tr("正在复制内嵌库文件"), 10);

    if (!m_taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在复制内嵌库文件"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 10);
    }

    emit progressUpdated(tr("正在下载处理器依赖"), 20);

    if (!m_taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在下载处理器依赖"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 20);
    }

    downloadProcessorDependencies();
}

void ForgeNewInstallTask::onPreExecuteDone()
{
    doExecute();
}

void ForgeNewInstallTask::doExecute()
{
    // 处理器要以原版客户端 JAR 为输入（拆分/重命名/补丁），缺了就先失败并说明原因
    QString vanillaJar = m_instancePath + "/versions/" + m_profile.minecraft + "/"
                         + m_profile.minecraft + ".jar";
    if (!QFile::exists(vanillaJar))
    {
        emit completed(false, tr("未找到原版 %1 客户端，请先安装原版再装Forge").arg(m_profile.minecraft));
        return;
    }

    emit progressUpdated(tr("正在构建变量"), 30);

    if (!m_taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在构建变量"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 30);
    }

    buildVariables();

    if (m_cancelled)
    {
        emit completed(false, tr("安装已取消"));
        return;
    }

    emit progressUpdated(tr("正在执行安装处理器"), 40);

    if (!m_taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在执行安装处理器"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 40);
    }

    runProcessorsAsync();
}

void ForgeNewInstallTask::onExecuteDone()
{
    postExecute();
}

void ForgeNewInstallTask::postExecute()
{
    emit progressUpdated(tr("正在下载剩余库文件"), 80);

    if (!m_taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在下载剩余库文件"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 80);
    }

    // 官方安装器/MultiMC 的最后一步：按最终版本 JSON 把库补齐。
    // 下载是异步的，完成后由 finishPostExecute() 收尾
    downloadRemainingLibraries();
}

void ForgeNewInstallTask::finishPostExecute()
{
    if (m_cancelled)
    {
        emit completed(false, tr("安装已取消"));
        return;
    }

    emit progressUpdated(tr("正在清理临时文件"), 95);

    if (!m_taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在清理临时文件"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 95);
    }

    cleanupTempFiles();

    onPostExecuteDone();
}

void ForgeNewInstallTask::onPostExecuteDone()
{
    emit progressUpdated(tr("安装完成"), 100);
    emit completed(true, QString());
}

void ForgeNewInstallTask::parseInstallProfile()
{
    QString profilePath = m_tempDir + "/install_profile.json";
    if (!JarUtils::extractFromJar(m_installerJarPath, "install_profile.json", profilePath))
    {
        emit completed(false, tr("无法提取install_profile.json"));
        return;
    }

    QFile file(profilePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        emit completed(false, tr("无法读取install_profile.json"));
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        emit completed(false, tr("install_profile.json解析失败: %1").arg(parseError.errorString()));
        return;
    }

    m_profile = ForgeInstallProfile::fromJson(doc.object());
}

void ForgeNewInstallTask::extractVersionJson()
{
    QString jsonEntry = m_profile.json;
    if (jsonEntry.startsWith("/"))
    {
        jsonEntry = jsonEntry.mid(1);
    }

    if (jsonEntry.isEmpty())
    {
        qDebug() << "[ForgeNewInstallTask]" << "No version.json path in profile, skipping";
        return;
    }

    QString versionJsonPath = m_tempDir + "/version.json";
    if (!JarUtils::extractFromJar(m_installerJarPath, jsonEntry, versionJsonPath))
    {
        qWarning() << "[ForgeNewInstallTask]" << "Failed to extract version.json from installer";
        return;
    }

    QFile file(versionJsonPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "[ForgeNewInstallTask]" << "Failed to read extracted version.json";
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isObject())
    {
        m_baseVersionJson = doc.object();
    }
}

void ForgeNewInstallTask::copyEmbeddedLibraries()
{
    QString librariesPath = m_instancePath + "/libraries";
    QDir librariesDir(librariesPath);
    if (!librariesDir.exists())
    {
        librariesDir.mkpath(".");
    }

    QString tempExtractDir = m_tempDir + "/maven_extract";
    QDir tempDir(tempExtractDir);
    if (tempDir.exists())
    {
        tempDir.removeRecursively();
    }
    tempDir.mkpath(".");

    if (!JarUtils::extractJarDirectory(m_installerJarPath, "maven", tempExtractDir))
    {
        qWarning() << "[ForgeNewInstallTask]" << "Failed to extract maven directory from installer";
        emit completed(false, tr("无法提取内嵌库文件，请确保已安装JDK"));
        return;
    }

    QString extractedMavenPath = tempExtractDir + "/maven";
    QDir mavenDir(extractedMavenPath);
    if (!mavenDir.exists())
    {
        qWarning() << "[ForgeNewInstallTask]" << "No maven directory found after extraction";
        QDir(tempExtractDir).removeRecursively();
        return;
    }

    QStringList jarFiles;
    QDirIterator it(extractedMavenPath,
                    QStringList() << "*.jar",
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        jarFiles.append(it.next());
    }

    int copiedCount = 0;
    for (const QString &srcFilePath : jarFiles)
    {
        QString relativePath = srcFilePath.mid(extractedMavenPath.length() + 1);
        QString destFilePath = librariesPath + "/" + relativePath;

        QFileInfo destFileInfo(destFilePath);
        if (destFileInfo.exists())
        {
            continue;
        }

        QDir destDir = destFileInfo.absoluteDir();
        if (!destDir.exists())
        {
            destDir.mkpath(".");
        }

        if (QFile::copy(srcFilePath, destFilePath))
        {
            copiedCount++;
        }
        else
        {
            qWarning() << "[ForgeNewInstallTask]" << "Failed to copy library:" << relativePath;
        }
    }

    QDir(tempExtractDir).removeRecursively();

    qDebug() << "[ForgeNewInstallTask]" << "Copied" << copiedCount << "embedded libraries";
}

void ForgeNewInstallTask::downloadProcessorDependencies()
{
    QSet<QString> libNames;

    for (const auto &proc : m_profile.processors)
    {
        libNames.insert(proc.jar);
        for (const QString &cp : proc.classpath)
        {
            libNames.insert(cp);
        }
    }

    for (const QJsonValue &libValue : m_profile.libraries)
    {
        if (libValue.isObject())
        {
            QJsonObject libObj = libValue.toObject();
            QString name = libObj["name"].toString();
            if (!name.isEmpty())
            {
                libNames.insert(name);
            }
        }
    }

    QString librariesPath = m_instancePath + "/libraries";
    QVector<QPair<QStringList, QString>> tasks;

    for (const QString &libName : libNames)
    {
        QString path = getLibraryPath(libName);
        if (path.isEmpty())
        {
            continue;
        }

        QString filePath = librariesPath + "/" + path;
        if (QFile::exists(filePath))
        {
            continue;
        }

        ForgeDownloader downloader;
        ForgeDownloadSource source = SettingsManager::instance()->getForgeDownloadSource();
        downloader.setDownloadSource(
            source == ForgeDownloadSource::Official ? DownloadSource::Official : DownloadSource::BMCL);

        QStringList urls = downloader.getCandidateUrls(path);
        if (urls.isEmpty())
        {
            continue;
        }

        tasks.append({urls, filePath});
    }

    if (tasks.isEmpty())
    {
        onPreExecuteDone();
        return;
    }

    ForgeDownloader *batchDownloader = new ForgeDownloader(this);
    ForgeDownloadSource source = SettingsManager::instance()->getForgeDownloadSource();
    batchDownloader->setDownloadSource(
        source == ForgeDownloadSource::Official ? DownloadSource::Official : DownloadSource::BMCL);

    connect(batchDownloader, &ForgeDownloader::downloadBytesProgress, this,
            [this](qint64 bytesReceived, qint64 bytesTotal)
            {
                if (!m_taskId.isEmpty()) {
                    DownloadTaskManager::instance()->updateTaskProgress(m_taskId, 20, bytesReceived, bytesTotal);
                }
            });

    batchDownloader->downloadFiles(tasks,
        [this, totalTasks = tasks.size()](int completed, int total)
        {
            Q_UNUSED(total);
            int percent = 20 + (completed * 10 / totalTasks);
            emit progressUpdated(tr("正在下载处理器依赖"), percent);
            if (!m_taskId.isEmpty()) {
                DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, percent);
            }
        },
        [this, batchDownloader](bool allSuccess, const QStringList &failed)
        {
            batchDownloader->deleteLater();
            if (!allSuccess) {
                qWarning() << "[ForgeNewInstallTask]" << "Some processor dependencies failed to download:" << failed;
                emit completed(false, tr("处理器依赖下载失败: %1").arg(failed.join(", ")));
            }
            else
            {
                onPreExecuteDone();
            }
        });
}

void ForgeNewInstallTask::buildVariables()
{
    m_variables["ROOT"] = m_instancePath;
    m_variables["MINECRAFT"] = m_instancePath;
    m_variables["INSTALLER"] = m_installerJarPath;
    m_variables["SIDE"] = "client";

    QString mcVersion = m_profile.minecraft;
    m_variables["MINECRAFT_VERSION"] = mcVersion;
    m_variables["MC_VERSION"] = mcVersion;

    QString forgeVersion = m_profile.version;
    m_variables["FORGE_VERSION"] = forgeVersion;
    m_variables["FORGE"] = QString("%1-%2").arg(mcVersion, forgeVersion);

    m_variables["LIBRARY_DIR"] = m_instancePath + "/libraries";

    QString gameJar = m_instancePath + "/versions/" + mcVersion + "/" + mcVersion + ".jar";
    m_variables["MINECRAFT_JAR"] = gameJar;
    m_variables["CLIENT"] = gameJar;

    m_variables["ASSET_INDEX"] = QString(mcVersion).replace('.', '_');

    if (!m_profile.data.isEmpty())
    {
        for (auto it = m_profile.data.begin(); it != m_profile.data.end(); ++it)
        {
            QString key = it.key();
            QJsonValue rawValue = it.value();

            if (rawValue.isObject())
            {
                QJsonObject valueObj = rawValue.toObject();
                if (valueObj.contains("client"))
                {
                    QString value = valueObj["client"].toString();

                    if (value.startsWith("[") && value.endsWith("]"))
                    {
                        QString libName = value.mid(1, value.length() - 2);
                        value = m_instancePath + "/libraries/" + getLibraryPath(libName);
                    }
                    else if (value.startsWith("'") && value.endsWith("'"))
                    {
                        value = value.mid(1, value.length() - 2);
                    }
                    else
                    {
                        // 安装器内嵌数据文件（如 /data/client.lzma），ZIP 条目名不带前导斜杠
                        QString entry = value;
                        while (entry.startsWith('/'))
                        {
                            entry = entry.mid(1);
                        }
                        QString filePath = m_tempDir + "/data_" + key;
                        if (JarUtils::extractFromJar(m_installerJarPath, entry, filePath))
                        {
                            value = filePath;
                        }
                        else if (entry != value && JarUtils::extractFromJar(m_installerJarPath, value, filePath))
                        {
                            value = filePath;
                        }
                    }

                    m_variables[key] = value;
                }
                else if (valueObj.contains("value"))
                {
                    m_variables[key] = valueObj["value"].toString();
                }
            }
            else if (rawValue.isString())
            {
                m_variables[key] = rawValue.toString();
            }
        }
    }
}

void ForgeNewInstallTask::runProcessors()
{
    QString javaPath = getJavaPath();
    if (javaPath.isEmpty())
    {
        emit completed(false, tr("未找到Java运行环境"));
        return;
    }

    QVector<ForgeProcessor> clientProcessors;
    for (const auto &proc : m_profile.processors)
    {
        if (proc.sides.isEmpty())
        {
            clientProcessors.append(proc);
        }
        else
        {
            for (const QString &side : proc.sides)
            {
                if (side == "client")
                {
                    clientProcessors.append(proc);
                    break;
                }
            }
        }
    }

    m_processorTotal = clientProcessors.size();
    m_processorDone = 0;

    for (int i = 0; i < clientProcessors.size(); ++i)
    {
        if (m_cancelled)
        {
            return;
        }

        const ForgeProcessor &processor = clientProcessors[i];

        bool skipProcessor = false;
        if (!processor.outputs.isEmpty())
        {
            skipProcessor = true;
            for (auto it = processor.outputs.begin(); it != processor.outputs.end(); ++it)
            {
                QString outputKey = parseLiteral(it.key(), m_variables);
                QString outputValue = parseLiteral(it.value(), m_variables);

                QFileInfo outputFile(outputKey);
                if (!outputFile.exists())
                {
                    skipProcessor = false;
                    break;
                }

                if (outputValue.length() == 40)
                {
                    QString fileHash = calculateSha1(outputKey);
                    if (fileHash != outputValue)
                    {
                        QFile::remove(outputKey);
                        skipProcessor = false;
                        break;
                    }
                }
            }
        }

        if (skipProcessor)
        {
            qDebug() << "[ForgeNewInstallTask]" << "Skipping processor (outputs exist):" << processor.jar;
            m_processorDone++;
            int percent = 40 + (m_processorDone * 30 / m_processorTotal);
            emit progressUpdated(tr("正在执行安装处理器"), percent);
            if (!m_taskId.isEmpty()) {
                DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, percent);
            }
            continue;
        }

        QString jarPath = m_instancePath + "/libraries/" + getLibraryPath(processor.jar);
        if (!QFile::exists(jarPath))
        {
            emit completed(false, tr("处理器依赖缺失: %1").arg(processor.jar));
            return;
        }

        QString mainClass = readManifestMainClass(jarPath);
        if (mainClass.isEmpty())
        {
            if (processor.jar.contains("ForgeAutoRenamingTool"))
            {
                mainClass = "net.minecraftforge.fart.Main";
            }
            else if (processor.jar.contains("ForgeFlower"))
            {
                mainClass = "org.jetbrains.java.decompiler.main.Main";
            }
            else if (processor.jar.contains("jarsplitter"))
            {
                mainClass = "net.minecraftforge.jarsplitter.ConsoleTool";
            }
            else if (processor.jar.contains("binarypatcher"))
            {
                mainClass = "net.minecraftforge.binarypatcher.ConsoleTool";
            }
            else if (processor.jar.contains("ForgeInstaller"))
            {
                mainClass = "net.minecraftforge.installer.actions.ClientInstall";
            }
            else
            {
                mainClass = "net.minecraftforge.installertools.ConsoleTool";
            }
            qDebug() << "[ForgeNewInstallTask]" << "Using fallback Main-Class for" << processor.jar << ":" << mainClass;
        }

        QStringList classpath;
        classpath.append(jarPath);

        for (const QString &cp : processor.classpath)
        {
            QString cpPath = m_instancePath + "/libraries/" + getLibraryPath(cp);
            classpath.append(cpPath);
        }

        classpath.append(m_installerJarPath);

        QStringList args;
        args << "-cp" << classpath.join(QDir::listSeparator());
        args << mainClass;

        for (const QJsonValue &argValue : processor.args)
        {
            QString arg;

            if (argValue.isString())
            {
                arg = argValue.toString();
            }
            else if (argValue.isObject())
            {
                QJsonObject argObj = argValue.toObject();
                if (argObj.contains("value"))
                {
                    QJsonValue val = argObj["value"];
                    if (val.isString())
                    {
                        arg = val.toString();
                    }
                    else if (val.isArray())
                    {
                        QStringList parts;
                        for (const QJsonValue &v : val.toArray())
                        {
                            parts.append(v.toString());
                        }
                        arg = parts.join(" ");
                    }
                }
                else if (argObj.contains("rules"))
                {
                    continue;
                }
            }
            else
            {
                continue;
            }

            if (arg.isEmpty()) continue;
            arg = parseLiteral(arg, m_variables);
            args << arg;
        }

        QProcess process;
        qDebug() << "[ForgeNewInstallTask]" << "Executing processor:" << processor.jar;
        qDebug() << "[ForgeNewInstallTask]" << "Working directory:" << m_instancePath;
        qDebug() << "[ForgeNewInstallTask]" << "Command:" << javaPath << args.join(" ");

        process.setWorkingDirectory(m_instancePath);
        process.start(javaPath, args);

        if (!process.waitForStarted())
        {
            QString errorOutput = QString::fromUtf8(process.readAllStandardError());
            emit completed(false, tr("无法启动处理器: %1\n%2").arg(processor.jar, errorOutput.left(500)));
            return;
        }

        if (!process.waitForFinished(600000))
        {
            emit completed(false, tr("处理器执行超时: %1").arg(processor.jar));
            return;
        }

        if (process.exitCode() != 0)
        {
            QString errorOutput = QString::fromUtf8(process.readAllStandardError());
            qWarning() << "[ForgeNewInstallTask]" << "Processor failed:" << processor.jar << errorOutput;
            emit completed(false, tr("处理器执行失败: %1\n%2").arg(processor.jar, errorOutput));
            return;
        }

        if (!processor.outputs.isEmpty())
        {
            for (auto it = processor.outputs.begin(); it != processor.outputs.end(); ++it)
            {
                QString outputKey = parseLiteral(it.key(), m_variables);
                QString outputValue = parseLiteral(it.value(), m_variables);

                if (!QFile::exists(outputKey))
                {
                    emit completed(false, tr("处理器输出文件缺失: %1").arg(outputKey));
                    return;
                }

                if (outputValue.length() == 40)
                {
                    QString fileHash = calculateSha1(outputKey);
                    if (fileHash != outputValue)
                    {
                        emit completed(false, tr("处理器输出文件校验失败: %1").arg(outputKey));
                        return;
                    }
                }
            }
        }

        m_processorDone++;
        int percent = 40 + (m_processorDone * 30 / m_processorTotal);
        emit progressUpdated(tr("正在执行安装处理器"), percent);
        if (!m_taskId.isEmpty()) {
            DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, percent);
        }
    }
}

void ForgeNewInstallTask::runProcessorsAsync()
{
    QString javaPath = getJavaPath();
    if (javaPath.isEmpty())
    {
        emit completed(false, tr("未找到可用的 Java，请先在设置的 Java 管理中选择或下载一个 Java"));
        return;
    }

    QVector<ForgeProcessor> clientProcessors;
    for (const auto &proc : m_profile.processors)
    {
        if (proc.sides.isEmpty())
        {
            clientProcessors.append(proc);
        }
        else
        {
            for (const QString &side : proc.sides)
            {
                if (side == "client")
                {
                    clientProcessors.append(proc);
                    break;
                }
            }
        }
    }

    m_processorTotal = clientProcessors.size();
    m_processorDone = 0;

    QString instancePath = m_instancePath;
    QString installerJarPath = m_installerJarPath;
    QMap<QString, QString> variables = m_variables;
    QString taskId = m_taskId;

    auto future2 = QtConcurrent::run([this, javaPath, clientProcessors, instancePath,
                       installerJarPath, variables, taskId]()
    {
        int processorTotal = clientProcessors.size();
        int processorDone = 0;

        for (int i = 0; i < clientProcessors.size(); ++i)
        {
            if (m_cancelled)
            {
                QMetaObject::invokeMethod(this, [this]()
                {
                    emit completed(false, tr("安装已取消"));
                }, Qt::QueuedConnection);
                return;
            }

            const ForgeProcessor &processor = clientProcessors[i];

            bool skipProcessor = false;
            if (!processor.outputs.isEmpty())
            {
                skipProcessor = true;
                for (auto it = processor.outputs.begin(); it != processor.outputs.end(); ++it)
                {
                    QString outputKey = parseLiteral(it.key(), variables);
                    QString outputValue = parseLiteral(it.value(), variables);

                    QFileInfo outputFile(outputKey);
                    if (!outputFile.exists())
                    {
                        skipProcessor = false;
                        break;
                    }

                    if (outputValue.length() == 40)
                    {
                        QString fileHash = calculateSha1(outputKey);
                        if (fileHash != outputValue)
                        {
                            QFile::remove(outputKey);
                            skipProcessor = false;
                            break;
                        }
                    }
                }
            }

            if (skipProcessor)
            {
                qDebug() << "[ForgeNewInstallTask]" << "Skipping processor (outputs exist):" << processor.jar;
                processorDone++;
                int percent = 40 + (processorDone * 30 / processorTotal);
                QMetaObject::invokeMethod(this, [this, percent, taskId]()
                {
                    emit progressUpdated(tr("正在执行安装处理器"), percent);
                    if (!taskId.isEmpty()) {
                        DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, percent);
                    }
                }, Qt::QueuedConnection);
                continue;
            }

            QString jarPath = instancePath + "/libraries/" + getLibraryPath(processor.jar);
            if (!QFile::exists(jarPath))
            {
                QString error = tr("处理器依赖缺失: %1").arg(processor.jar);
                QMetaObject::invokeMethod(this, [this, error]()
                {
                    emit completed(false, error);
                }, Qt::QueuedConnection);
                return;
            }

            QString mainClass = readManifestMainClass(jarPath);
            if (mainClass.isEmpty())
            {
                if (processor.jar.contains("ForgeAutoRenamingTool"))
                {
                    mainClass = "net.minecraftforge.fart.Main";
                }
                else if (processor.jar.contains("ForgeFlower"))
                {
                    mainClass = "org.jetbrains.java.decompiler.main.Main";
                }
                else
                {
                    qDebug() << "[ForgeNewInstallTask]" << "Using fallback Main-Class for" << processor.jar << ":" << mainClass;
                }
            }

            QStringList classpath;
            classpath.append(jarPath);

            for (const QString &cp : processor.classpath)
            {
                QString cpPath = instancePath + "/libraries/" + getLibraryPath(cp);
                classpath.append(cpPath);
            }

            classpath.append(installerJarPath);

            QStringList args;
            args << "-cp" << classpath.join(QDir::listSeparator());
            args << mainClass;

            for (const QJsonValue &argValue : processor.args)
            {
                QString arg;

                if (argValue.isString())
                {
                    arg = argValue.toString();
                }
                else if (argValue.isObject())
                {
                    QJsonObject argObj = argValue.toObject();
                    if (argObj.contains("value"))
                    {
                        QJsonValue val = argObj["value"];
                        if (val.isString())
                        {
                            arg = val.toString();
                        }
                        else if (val.isArray())
                        {
                            QStringList parts;
                            for (const QJsonValue &v : val.toArray())
                            {
                                parts.append(v.toString());
                            }
                            arg = parts.join(" ");
                        }
                    }
                    else if (argObj.contains("rules"))
                    {
                        continue;
                    }
                }
                else
                {
                    continue;
                }

                if (arg.isEmpty()) continue;
                arg = parseLiteral(arg, variables);
                args << arg;
            }

            QProcess process;
            // 合并输出：部分工具把错误打到 stdout，只看 stderr 会拿到空信息
            process.setProcessChannelMode(QProcess::MergedChannels);
            qDebug() << "[ForgeNewInstallTask]" << "Executing processor:" << processor.jar;
            qDebug() << "[ForgeNewInstallTask]" << "Working directory:" << instancePath;
            qDebug() << "[ForgeNewInstallTask]" << "Command:" << javaPath << args.join(" ");

            process.setWorkingDirectory(instancePath);
            process.start(javaPath, args);

            if (!process.waitForStarted())
            {
                QString errorOutput = QString::fromUtf8(process.readAll());
                QString error = tr("无法启动处理器: %1\n%2").arg(processor.jar, errorOutput.left(500));
                QMetaObject::invokeMethod(this, [this, error]()
                {
                    emit completed(false, error);
                }, Qt::QueuedConnection);
                return;
            }

            if (!process.waitForFinished(600000))
            {
                QString error = tr("处理器执行超时: %1").arg(processor.jar);
                QMetaObject::invokeMethod(this, [this, error]()
                {
                    emit completed(false, error);
                }, Qt::QueuedConnection);
                return;
            }

            if (process.exitCode() != 0)
            {
                QString errorOutput = QString::fromUtf8(process.readAll());
                qWarning() << "[ForgeNewInstallTask]" << "Processor failed:" << processor.jar
                           << "exit code:" << process.exitCode() << errorOutput;
                QString error = tr("处理器执行失败 (退出码 %1): %2\n%3")
                                    .arg(process.exitCode())
                                    .arg(processor.jar, errorOutput.left(800));
                QMetaObject::invokeMethod(this, [this, error]()
                {
                    emit completed(false, error);
                }, Qt::QueuedConnection);
                return;
            }

            if (!processor.outputs.isEmpty())
            {
                for (auto it = processor.outputs.begin(); it != processor.outputs.end(); ++it)
                {
                    QString outputKey = parseLiteral(it.key(), variables);
                    QString outputValue = parseLiteral(it.value(), variables);

                    if (!QFile::exists(outputKey))
                    {
                        QString error = tr("处理器输出文件缺失: %1").arg(outputKey);
                        QMetaObject::invokeMethod(this, [this, error]()
                        {
                            emit completed(false, error);
                        }, Qt::QueuedConnection);
                        return;
                    }

                    if (outputValue.length() == 40)
                    {
                        QString fileHash = calculateSha1(outputKey);
                        if (fileHash != outputValue)
                        {
                            QString error = tr("处理器输出文件校验失败: %1").arg(outputKey);
                            QMetaObject::invokeMethod(this, [this, error]()
                            {
                                emit completed(false, error);
                            }, Qt::QueuedConnection);
                            return;
                        }
                    }
                }
            }

            processorDone++;
            int percent = 40 + (processorDone * 30 / processorTotal);
            QMetaObject::invokeMethod(this, [this, percent, taskId]()
            {
                emit progressUpdated(tr("正在执行安装处理器"), percent);
                if (!taskId.isEmpty()) {
                    DownloadTaskManager::instance()->updateTaskProgressPercent(taskId, percent);
                }
            }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(this, [this, processorDone]()
        {
            m_processorDone = processorDone;
            onRunProcessorsDone(true, QString());
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(future2);
}

void ForgeNewInstallTask::onRunProcessorsDone(bool success, const QString &error)
{
    if (!success)
    {
        emit completed(false, error);
        return;
    }

    emit progressUpdated(tr("正在生成版本配置"), 70);

    if (!m_taskId.isEmpty()) {
        DownloadTaskManager::instance()->updateTaskStatus(m_taskId, DownloadTaskStatus::Downloading,
                                                          tr("正在生成版本配置"));
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 70);
    }

    generateVersionJson();

    if (m_cancelled)
    {
        emit completed(false, tr("安装已取消"));
        return;
    }

    emit progressUpdated(tr("正在准备安装"), 75);

    onExecuteDone();
}

void ForgeNewInstallTask::generateVersionJson()
{
    QJsonObject versionJson = m_baseVersionJson;

    QString mcVersion = m_profile.minecraft;
    // install_profile 的 version 字段是完整版本号（如 1.20.1-forge-47.3.0），补丁版本号要剥掉前缀
    QString forgeVersion = m_profile.version;
    const QString fullPrefix = mcVersion + "-forge-";
    if (forgeVersion.startsWith(fullPrefix))
    {
        forgeVersion = forgeVersion.mid(fullPrefix.length());
    }

    if (versionJson.isEmpty())
    {
        QString forgeVersionId = QString("%1-forge-%2").arg(mcVersion, forgeVersion);

        versionJson["id"] = forgeVersionId;
        versionJson["inheritsFrom"] = mcVersion;
        versionJson["type"] = "release";
        versionJson["mainClass"] = "cpw.mods.modlauncher.Launcher";

        QJsonArray libraries;
        QJsonObject forgeLib;
        forgeLib["name"] = QString("net.minecraftforge:forge:%1-%2").arg(mcVersion, forgeVersion);
        QJsonObject forgeDownloads;
        QJsonObject forgeArtifact;
        forgeArtifact["url"] = QString("https://maven.minecraftforge.net/net/minecraftforge/forge/%1-%2/forge-%1-%2.jar")
                                  .arg(mcVersion, forgeVersion);
        forgeDownloads["artifact"] = forgeArtifact;
        forgeLib["downloads"] = forgeDownloads;
        libraries.append(forgeLib);
        versionJson["libraries"] = libraries;
    }
    else
    {
        QString versionId = versionJson["id"].toString();
        if (versionId.isEmpty())
        {
            versionJson["id"] = QString("%1-forge-%2").arg(mcVersion, forgeVersion);
        }

        if (!versionJson.contains("inheritsFrom"))
        {
            versionJson["inheritsFrom"] = mcVersion;
        }

        if (!versionJson.contains("mainClass"))
        {
            versionJson["mainClass"] = "cpw.mods.modlauncher.Launcher";
        }

        QJsonDocument doc(versionJson);
        QString jsonStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        jsonStr.replace("[version]", forgeVersion);
        jsonStr.replace("[mcversion]", mcVersion);
        QJsonDocument processedDoc = QJsonDocument::fromJson(jsonStr.toUtf8());
        if (processedDoc.isObject())
        {
            versionJson = processedDoc.object();
        }

        QJsonArray libraries = versionJson["libraries"].toArray();
        for (int i = 0; i < libraries.size(); ++i)
        {
            QJsonObject lib = libraries[i].toObject();
            QString name = lib["name"].toString();
            if (name.isEmpty())
            {
                continue;
            }

            if (!lib.contains("downloads"))
            {
                QString path = getLibraryPath(name);
                if (!path.isEmpty())
                {
                    QJsonObject downloads;
                    QJsonObject artifact;
                    artifact["path"] = path;
                    artifact["url"] = QString("https://libraries.minecraft.net/%1").arg(path);
                    downloads["artifact"] = artifact;
                    lib["downloads"] = downloads;
                }
            }

            libraries[i] = lib;
        }
        versionJson["libraries"] = libraries;
    }

    QString versionId = versionJson["id"].toString();
    QString versionDir = m_instancePath + "/versions/" + versionId;
    QString versionJsonPath = versionDir + "/" + versionId + ".json";

    QDir dir(versionDir);
    if (!dir.exists())
    {
        dir.mkpath(".");
    }

    QFile versionFile(versionJsonPath);
    if (!versionFile.open(QIODevice::WriteOnly))
    {
        emit completed(false, tr("无法写入版本配置文件: %1").arg(versionJsonPath));
        return;
    }

    QJsonDocument doc(versionJson);
    versionFile.write(doc.toJson());
    versionFile.close();

    m_writtenVersionId = versionId;
    m_writtenVersionJsonPath = versionJsonPath;

    // 官方安装器行为：把原版客户端 JAR 拷成 versions/<id>/<id>.jar，
    // 启动时 classpath 与 ignoreList 里的 ${version_name}.jar 都指向它
    const QString vanillaJar = m_instancePath + "/versions/" + mcVersion + "/" + mcVersion + ".jar";
    const QString targetJar = versionDir + "/" + versionId + ".jar";
    if (QFile::exists(vanillaJar) && !QFile::exists(targetJar))
    {
        if (!QFile::copy(vanillaJar, targetJar))
        {
            qWarning() << "[ForgeNewInstallTask]" << "Failed to copy vanilla jar to" << targetJar;
        }
    }
}

void ForgeNewInstallTask::downloadRemainingLibraries()
{
    // MultiMC 式收尾：按生成的版本 JSON 把所有库补齐到 <游戏根>/libraries。
    // 多源候选（官方 Forge Maven / Mojang / BMCLAPI）+ SHA1 校验，
    // 全部装进一个 NetJob 由 net 层做 6 路并发
    if (m_writtenVersionJsonPath.isEmpty())
    {
        finishPostExecute();
        return;
    }

    QFile versionFile(m_writtenVersionJsonPath);
    if (!versionFile.open(QIODevice::ReadOnly))
    {
        finishPostExecute();
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(versionFile.readAll());
    versionFile.close();

    const QJsonArray libraries = doc.object()["libraries"].toArray();
    QString librariesPath = m_instancePath + "/libraries";

    ForgeDownloader sourceProvider;
    ForgeDownloadSource source = SettingsManager::instance()->getForgeDownloadSource();
    sourceProvider.setDownloadSource(
        source == ForgeDownloadSource::Official ? DownloadSource::Official : DownloadSource::BMCL);

    QVector<ForgeFileDownload> tasks;
    int reusedCount = 0;
    for (const QJsonValue &libValue : libraries)
    {
        if (!libValue.isObject())
        {
            continue;
        }
        QJsonObject libObj = libValue.toObject();
        QString name = libObj["name"].toString();
        if (name.isEmpty())
        {
            continue;
        }

        QString relPath;
        QString expectedSha1;
        QString directUrl;
        if (libObj.contains("downloads") && libObj["downloads"].isObject())
        {
            QJsonObject artifact = libObj["downloads"].toObject()["artifact"].toObject();
            relPath = artifact["path"].toString();
            expectedSha1 = artifact["sha1"].toString();
            directUrl = artifact["url"].toString();
        }
        if (relPath.isEmpty())
        {
            relPath = getLibraryPath(name);
        }
        if (relPath.isEmpty())
        {
            continue;
        }

        QString filePath = librariesPath + "/" + relPath;
        QFileInfo info(filePath);
        if (info.exists())
        {
            if (expectedSha1.isEmpty())
            {
                ++reusedCount;
                continue;
            }
            // 已存在但内容不对的库重新下载（MultiMC 的校验行为）
            if (calculateSha1(filePath) == expectedSha1)
            {
                ++reusedCount;
                continue;
            }
            QFile::remove(filePath);
        }

        ForgeFileDownload task;
        task.filePath = filePath;
        task.expectedSha1 = expectedSha1;

        // 按下载源排好的多源候选优先（BMCL 源时国内镜像在前），
        // 官方精确文件地址殿后兜底；重复地址去重
        const QStringList candidates = sourceProvider.getCandidateUrls(relPath);
        for (const QString &url : candidates)
        {
            if (!task.urls.contains(url))
            {
                task.urls.append(url);
            }
        }
        if (!directUrl.isEmpty() && !task.urls.contains(directUrl))
        {
            task.urls.append(directUrl);
        }
        if (task.urls.isEmpty())
        {
            continue;
        }
        tasks.append(task);
    }

    if (reusedCount > 0)
    {
        qDebug() << "[ForgeNewInstallTask]" << "Reused" << reusedCount << "existing libraries";
    }

    if (tasks.isEmpty())
    {
        finishPostExecute();
        return;
    }

    if (m_libDownloader)
    {
        m_libDownloader->deleteLater();
    }
    m_libDownloader = new ForgeDownloader(this);
    ForgeDownloadSource source2 = SettingsManager::instance()->getForgeDownloadSource();
    m_libDownloader->setDownloadSource(
        source2 == ForgeDownloadSource::Official ? DownloadSource::Official : DownloadSource::BMCL);

    const int totalTasks = tasks.size();
    connect(m_libDownloader, &ForgeDownloader::downloadBytesProgress, this,
            [this](qint64 bytesReceived, qint64 bytesTotal)
            {
                if (!m_taskId.isEmpty())
                {
                    DownloadTaskManager::instance()->updateTaskProgress(m_taskId, 80, bytesReceived, bytesTotal);
                }
            });

    m_libDownloader->downloadFiles(tasks,
        [this, totalTasks](int completed, int total)
        {
            Q_UNUSED(total);
            int percent = 80 + (completed * 15 / totalTasks);
            emit progressUpdated(tr("正在下载剩余库文件"), percent);
            if (!m_taskId.isEmpty())
            {
                DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, percent);
            }
        },
        [this](bool allSuccess, const QStringList &failed)
        {
            if (!allSuccess)
            {
                qWarning() << "[ForgeNewInstallTask]" << "Some libraries failed to download:" << failed;
                emit completed(false, tr("库文件下载失败: %1").arg(failed.join(", ")));
                return;
            }
            finishPostExecute();
        });
}

void ForgeNewInstallTask::cleanupTempFiles()
{
    if (!m_tempDir.isEmpty())
    {
        QDir tempDir(m_tempDir);
        if (tempDir.exists())
        {
            tempDir.removeRecursively();
        }
        m_tempDir.clear();
    }
}

QString ForgeNewInstallTask::getLibraryPath(const QString &libraryName)
{
    QStringList parts = libraryName.split(':');
    if (parts.size() < 3)
    {
        return QString();
    }

    QString group = QString(parts[0]).replace('.', '/');
    QString artifact = parts[1];
    QString version = parts[2];
    QString classifier = parts.size() > 3 ? "-" + parts[3] : "";

    QString extension = "jar";
    int atIndex = version.indexOf('@');
    if (atIndex != -1)
    {
        extension = version.mid(atIndex + 1);
        version = version.left(atIndex);
    }

    return QString("%1/%2/%3/%2-%3%4.%5")
           .arg(group, artifact, version, classifier, extension);
}

namespace {

// 缓存已验证可用的 Java：处理器会多次启动 Java，-version 验证只做一次
QMutex &validatedJavaMutex()
{
    static QMutex mutex;
    return mutex;
}
QString &validatedJavaPath()
{
    static QString path;
    return path;
}

/// 验证某个 java 可执行文件真的能跑（PATH 里可能残留卸载后的空壳 java.exe）
bool javaExecutableWorks(const QString &javaPath)
{
    if (javaPath.isEmpty())
    {
        return false;
    }
    QProcess process;
    process.start(javaPath, QStringList() << QStringLiteral("-version"));
    if (!process.waitForStarted(5000) || !process.waitForFinished(10000))
    {
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            process.waitForFinished(2000);
        }
        return false;
    }
    return process.exitCode() == 0;
}

/// 常见安装位置里快速找一个能用的 java（只查目录，不扫盘）
QString findJavaInCommonLocations()
{
    QStringList roots;
#ifdef Q_OS_WIN
    roots << QStringLiteral("C:/Program Files/Java")
          << QStringLiteral("C:/Program Files (x86)/Java")
          << QStringLiteral("C:/Program Files/Microsoft")
          << QStringLiteral("C:/Program Files/Eclipse Adoptium")
          << QStringLiteral("C:/Program Files/Zulu");
#else
    roots << QStringLiteral("/usr/lib/jvm") << QStringLiteral("/usr/local/lib/jvm");
#endif
    QStringList candidates;
    for (const QString &root : roots)
    {
        QDir dir(root);
        if (!dir.exists())
        {
            continue;
        }
        const QStringList jdkDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                  QDir::Name | QDir::Reversed);
        for (const QString &jdk : jdkDirs)
        {
#ifdef Q_OS_WIN
            candidates << root + QStringLiteral("/") + jdk + QStringLiteral("/bin/java.exe");
#else
            candidates << root + QStringLiteral("/") + jdk + QStringLiteral("/bin/java");
#endif
        }
    }
    for (const QString &candidate : candidates)
    {
        if (QFile::exists(candidate) && javaExecutableWorks(candidate))
        {
            return candidate;
        }
    }
    return QString();
}

} // namespace

QString ForgeNewInstallTask::getJavaPath()
{
    // 之前已验证过的直接复用
    {
        QMutexLocker locker(&validatedJavaMutex());
        if (!validatedJavaPath().isEmpty())
        {
            return validatedJavaPath();
        }
    }

    // 用户明确指定了 Java（且不是「自动选择」的 auto 字面量）时优先使用
    const QString configured = SettingsManager::instance()->getJavaPath();
    if (!configured.isEmpty()
        && configured.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0
        && QFile::exists(configured)
        && javaExecutableWorks(configured))
    {
        QMutexLocker locker(&validatedJavaMutex());
        validatedJavaPath() = configured;
        return configured;
    }

    // 自动模式：复用启动器给游戏选 Java 的同一套逻辑（按 MC 版本约束选版本）。
    // 只在已有缓存清单时走这条路，避免现场扫描磁盘卡住界面
    const QList<QPair<QString, QString>> cached =
        SettingsManager::instance()->getJavaInstallations();
    if (!cached.isEmpty())
    {
        GameLauncher::JavaInfo info =
            GameLauncher::instance()->findBestJavaVersion(m_profile.minecraft);
        if (info.valid && QFile::exists(info.path) && javaExecutableWorks(info.path))
        {
            QMutexLocker locker(&validatedJavaMutex());
            validatedJavaPath() = info.path;
            return info.path;
        }
    }

    // JAVA_HOME
    QString javaPath = qEnvironmentVariable("JAVA_HOME");
    if (!javaPath.isEmpty())
    {
#ifdef Q_OS_WIN
        javaPath += QStringLiteral("/bin/java.exe");
#else
        javaPath += QStringLiteral("/bin/java");
#endif
        if (QFile::exists(javaPath) && javaExecutableWorks(javaPath))
        {
            QMutexLocker locker(&validatedJavaMutex());
            validatedJavaPath() = javaPath;
            return javaPath;
        }
    }

    // 常见安装目录（只查固定位置，快）
    const QString found = findJavaInCommonLocations();
    if (!found.isEmpty())
    {
        QMutexLocker locker(&validatedJavaMutex());
        validatedJavaPath() = found;
        return found;
    }

    // PATH 兜底：先验证（PATH 里常见卸载残留的空壳 java）
#ifdef Q_OS_WIN
    const QString pathJava = QStandardPaths::findExecutable(QStringLiteral("java.exe"));
#else
    const QString pathJava = QStandardPaths::findExecutable(QStringLiteral("java"));
#endif
    if (!pathJava.isEmpty() && javaExecutableWorks(pathJava))
    {
        QMutexLocker locker(&validatedJavaMutex());
        validatedJavaPath() = pathJava;
        return pathJava;
    }

    return QString();
}

QString ForgeNewInstallTask::readManifestMainClass(const QString &jarPath)
{
    QString tempManifest = QDir::tempPath() + "/blockbox_manifest_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".tmp";
    if (!JarUtils::extractFromJar(jarPath, "META-INF/MANIFEST.MF", tempManifest))
    {
        qWarning() << "[ForgeNewInstallTask]" << "Failed to extract MANIFEST.MF from:" << jarPath;
        return QString();
    }

    QFile file(tempManifest);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "[ForgeNewInstallTask]" << "Failed to read extracted MANIFEST.MF";
        QFile::remove(tempManifest);
        return QString();
    }

    QString mainClass;
    QTextStream stream(&file);
    while (!stream.atEnd())
    {
        QString line = stream.readLine().trimmed();
        if (line.startsWith("Main-Class:"))
        {
            mainClass = line.mid(QString("Main-Class:").length()).trimmed();
            break;
        }
    }

    file.close();
    QFile::remove(tempManifest);

    if (!mainClass.isEmpty())
    {
        qDebug() << "[ForgeNewInstallTask]" << "Read Main-Class from manifest:" << mainClass;
    }

    return mainClass;
}

QString ForgeNewInstallTask::replaceVariables(const QString &str, const QMap<QString, QString> &vars)
{
    QString result = str;

    static const QRegularExpression varPattern(R"(\$\{([^}]+)\})");
    QRegularExpressionMatchIterator it = varPattern.globalMatch(result);
    while (it.hasNext())
    {
        QRegularExpressionMatch match = it.next();
        QString varName = match.captured(1);
        if (vars.contains(varName))
        {
            result.replace(match.captured(0), vars[varName]);
        }
    }

    return result;
}

QString ForgeNewInstallTask::parseLiteral(const QString &literal, const QMap<QString, QString> &vars)
{
    if (literal.startsWith("{") && literal.endsWith("}"))
    {
        QString key = literal.mid(1, literal.length() - 2);
        return vars.value(key);
    }
    else if (literal.startsWith("'") && literal.endsWith("'"))
    {
        return literal.mid(1, literal.length() - 2);
    }
    else if (literal.startsWith("[") && literal.endsWith("]"))
    {
        QString libName = literal.mid(1, literal.length() - 2);
        return m_instancePath + "/libraries/" + getLibraryPath(libName);
    }
    else
    {
        return replaceVariables(literal, vars);
    }
}

QString ForgeNewInstallTask::calculateSha1(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    file.close();

    return hash.result().toHex();
}

} // namespace forge