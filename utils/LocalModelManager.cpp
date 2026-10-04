/**
 * @file   LocalModelManager.cpp
 * @brief  本地 AI 模型管理器实现 - 基于 Ollama 的本地大模型推理后端管理
 * @author BlockBox Team
 * @date   2026-08-06
 */

#include "LocalModelManager.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include "utils/DownloadTaskManager.h"
#include "utils/MultiThreadDownloader.h"

// ============================================================================
// 构造与析构
// ============================================================================

LocalModelManager::LocalModelManager(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_serviceProcess(nullptr)
    , m_pullProcess(nullptr)
    , m_removeProcess(nullptr)
    , m_installerDownloader(nullptr)
    , m_serviceStatus(ServiceStatus::Unknown)
{
}

LocalModelManager::~LocalModelManager()
{
    // 拉取/删除进程：尽量优雅终止
    if (m_pullProcess)
    {
        m_pullProcess->disconnect(this);
        m_pullProcess->terminate();
        if (!m_pullProcess->waitForFinished(2000))
        {
            m_pullProcess->kill();
        }
        m_pullProcess->deleteLater();
    }
    if (m_removeProcess)
    {
        m_removeProcess->disconnect(this);
        m_removeProcess->terminate();
        m_removeProcess->deleteLater();
    }
    // 服务进程：随对象销毁停止（仅停止本对象启动的）
    if (m_serviceProcess)
    {
        m_serviceProcess->disconnect(this);
        m_serviceProcess->terminate();
        if (!m_serviceProcess->waitForFinished(3000))
        {
            m_serviceProcess->kill();
        }
        m_serviceProcess->deleteLater();
    }
    if (m_installerDownloader)
    {
        m_installerDownloader->cancelDownload();
        m_installerDownloader->deleteLater();
    }
}

// ============================================================================
// 安装与服务状态
// ============================================================================

QString LocalModelManager::ollamaPath() const
{
    // 1. 通过 where ollama 查 PATH
    QProcess whereProc;
    whereProc.setProgram("where");
    whereProc.setArguments({"ollama"});
    whereProc.start(QIODevice::ReadOnly);
    if (whereProc.waitForFinished(2000) && whereProc.exitCode() == 0)
    {
        const QString out = QString::fromLocal8Bit(whereProc.readAllStandardOutput()).trimmed();
        const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        if (!lines.isEmpty())
        {
            const QString candidate = lines.first().trimmed();
            if (!candidate.isEmpty() && QFileInfo::exists(candidate))
            {
                return candidate;
            }
        }
    }

    // 2. 常见安装路径
    const QStringList candidates = {
        QStringLiteral("C:/Program Files/Ollama/ollama.exe"),
        QStringLiteral("C:/Program Files (x86)/Ollama/ollama.exe"),
        QDir::fromNativeSeparators(
            QProcessEnvironment::systemEnvironment().value("LOCALAPPDATA") +
            "/Programs/Ollama/ollama.exe"),
        QDir::fromNativeSeparators(
            QProcessEnvironment::systemEnvironment().value("USERPROFILE") +
            "/AppData/Local/Programs/Ollama/ollama.exe"),
    };
    for (const QString& path : candidates)
    {
        if (!path.isEmpty() && QFileInfo::exists(path))
        {
            return path;
        }
    }
    return {};
}

bool LocalModelManager::isOllamaInstalled() const
{
    return !ollamaPath().isEmpty();
}

void LocalModelManager::checkServiceStatus()
{
    // 未安装直接回报
    if (!isOllamaInstalled())
    {
        m_serviceStatus = ServiceStatus::NotInstalled;
        emit serviceStatusChecked(m_serviceStatus);
        return;
    }

    QNetworkRequest req(QUrl(ollamaBaseUrl() + "/api/tags"));
    req.setTransferTimeout(STATUS_CHECK_TIMEOUT_MS);
    QNetworkReply* reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, &LocalModelManager::onStatusReplyFinished);
}

void LocalModelManager::onStatusReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
    {
        return;
    }
    reply->deleteLater();

    const QNetworkReply::NetworkError err = reply->error();
    if (err == QNetworkReply::NoError)
    {
        m_serviceStatus = ServiceStatus::Running;
    }
    else
    {
        // 服务未运行，但已安装
        m_serviceStatus = ServiceStatus::InstalledStopped;
    }
    emit serviceStatusChecked(m_serviceStatus);
}

bool LocalModelManager::startService()
{
    if (m_serviceStatus == ServiceStatus::Running)
    {
        emit serviceStarted();
        return true;
    }
    const QString exe = ollamaPath();
    if (exe.isEmpty())
    {
        emit serviceStopped(tr("未找到 Ollama 可执行文件"));
        return false;
    }

    if (m_serviceProcess)
    {
        // 已经在启动中
        return true;
    }

    m_serviceProcess = new QProcess(this);
    m_serviceProcess->setProgram(exe);
    m_serviceProcess->setArguments({"serve"});
    // 把输出重定向，避免占用控制台
    m_serviceProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_serviceProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LocalModelManager::onServiceProcessFinished);
    connect(m_serviceProcess, &QProcess::errorOccurred,
            this, &LocalModelManager::onServiceProcessErrorOccurred);

    m_serviceProcess->start(QIODevice::ReadOnly);
    if (!m_serviceProcess->waitForStarted(3000))
    {
        emit serviceStopped(tr("无法启动 Ollama 服务：%1")
                                .arg(m_serviceProcess->errorString()));
        m_serviceProcess->deleteLater();
        m_serviceProcess = nullptr;
        return false;
    }

    // 启动后立即标记状态并通知；后续若服务真的就绪会通过 listModels/请求验证
    m_serviceStatus = ServiceStatus::Running;
    emit serviceStarted();

    // 延后探测一次状态，确认服务确实在响应（启动后通常需要 0.5~2s）
    QTimer::singleShot(1500, this, [this]() { checkServiceStatus(); });
    return true;
}

void LocalModelManager::stopService()
{
    if (!m_serviceProcess)
    {
        return;
    }
    m_serviceProcess->disconnect(this);
    m_serviceProcess->terminate();
    if (!m_serviceProcess->waitForFinished(3000))
    {
        m_serviceProcess->kill();
    }
    m_serviceProcess->deleteLater();
    m_serviceProcess = nullptr;
    m_serviceStatus = ServiceStatus::InstalledStopped;
    emit serviceStopped({});
}

void LocalModelManager::onServiceProcessFinished(int exitCode, QProcess::ExitStatus)
{
    Q_UNUSED(exitCode)
    if (!m_serviceProcess)
    {
        return;
    }
    m_serviceProcess->deleteLater();
    m_serviceProcess = nullptr;
    m_serviceStatus = ServiceStatus::InstalledStopped;
    emit serviceStopped(tr("Ollama 服务进程已退出"));
}

void LocalModelManager::onServiceProcessErrorOccurred(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    if (!m_serviceProcess)
    {
        return;
    }
    const QString msg = tr("Ollama 服务进程错误：%1")
                            .arg(m_serviceProcess->errorString());
    m_serviceProcess->deleteLater();
    m_serviceProcess = nullptr;
    m_serviceStatus = ServiceStatus::InstalledStopped;
    emit serviceStopped(msg);
}

// ============================================================================
// 模型管理
// ============================================================================

void LocalModelManager::pullModel(const QString& tag)
{
    if (m_pullProcess)
    {
        emit pullFailed(tag, tr("已有模型正在拉取中"));
        return;
    }
    const QString exe = ollamaPath();
    if (exe.isEmpty())
    {
        emit pullFailed(tag, tr("未找到 Ollama 可执行文件"));
        return;
    }

    m_pullProcess = new QProcess(this);
    m_pullProcess->setProgram(exe);
    m_pullProcess->setArguments({"pull", tag});
    m_pullProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_pullProcess, &QProcess::readyReadStandardOutput,
            this, &LocalModelManager::onPullReadyRead);
    connect(m_pullProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LocalModelManager::onPullFinished);
    connect(m_pullProcess, &QProcess::errorOccurred,
            this, &LocalModelManager::onPullErrorOccurred);

    m_pullProcess->start(QIODevice::ReadOnly);
    if (!m_pullProcess->waitForStarted(3000))
    {
        emit pullFailed(tag, tr("无法启动 ollama pull：%1")
                                .arg(m_pullProcess->errorString()));
        m_pullProcess->deleteLater();
        m_pullProcess = nullptr;
        return;
    }

    // 初始状态通知
    emit pullProgress(tag, 0, {}, tr("正在拉取 %1 ...").arg(tag));
}

void LocalModelManager::cancelPull()
{
    if (!m_pullProcess)
    {
        return;
    }
    m_pullProcess->disconnect(this);
    m_pullProcess->terminate();
    if (!m_pullProcess->waitForFinished(2000))
    {
        m_pullProcess->kill();
    }
    m_pullProcess->deleteLater();
    m_pullProcess = nullptr;
    emit pullFailed({}, tr("用户已取消拉取"));
}

void LocalModelManager::onPullReadyRead()
{
    if (!m_pullProcess)
    {
        return;
    }
    while (m_pullProcess->canReadLine())
    {
        const QByteArray lineBytes = m_pullProcess->readLine();
        const QString line = QString::fromUtf8(lineBytes).trimmed();
        if (line.isEmpty())
        {
            continue;
        }
        // 解析当前正在拉取的 tag（解析保留为 m_pullProcess 参数）
        // 这里取启动参数中第三个参数（pull 之后的 tag）
        QString currentTag;
        const auto args = m_pullProcess->arguments();
        if (args.size() >= 3)
        {
            currentTag = args.at(2);
        }
        parsePullLine(line, currentTag);
    }
}

void LocalModelManager::parsePullLine(const QString& line, const QString& tag)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
    {
        // 非 JSON 行（例如旧版本纯文本进度条），简单回显
        emit pullProgress(tag, -1, {}, line);
        return;
    }
    const QJsonObject obj = doc.object();
    const QString status = obj.value("status").toString();

    if (status == "success")
    {
        emit pullProgress(tag, 100, {}, tr("拉取完成"));
        return;
    }
    if (status == "pulling manifest")
    {
        emit pullProgress(tag, -1, {}, tr("正在拉取清单..."));
        return;
    }
    if (status == "verifying sha256 digest")
    {
        emit pullProgress(tag, -1, {}, tr("正在校验摘要..."));
        return;
    }
    if (status == "writing manifest")
    {
        emit pullProgress(tag, -1, {}, tr("正在写入清单..."));
        return;
    }
    if (status == "removing any unused layers")
    {
        emit pullProgress(tag, -1, {}, tr("正在清理冗余层..."));
        return;
    }
    if (status == "downloading")
    {
        const qint64 total = obj.value("total").toVariant().toLongLong();
        const qint64 completed = obj.value("completed").toVariant().toLongLong();
        int percent = -1;
        if (total > 0)
        {
            percent = static_cast<int>((completed * 100) / total);
        }
        emit pullProgress(tag, percent, {}, tr("正在下载 %1 / %2")
                                                .arg(formatBytes(completed))
                                                .arg(formatBytes(total)));
        return;
    }
    // 其他未知状态直接回显
    emit pullProgress(tag, -1, {}, status);
}

void LocalModelManager::onPullFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)
    QString currentTag;
    if (m_pullProcess)
    {
        const auto args = m_pullProcess->arguments();
        if (args.size() >= 3)
        {
            currentTag = args.at(2);
        }
        m_pullProcess->deleteLater();
        m_pullProcess = nullptr;
    }
    if (exitCode == 0)
    {
        emit pullFinished(currentTag);
    }
    else
    {
        emit pullFailed(currentTag, tr("ollama pull 退出码：%1").arg(exitCode));
    }
}

void LocalModelManager::onPullErrorOccurred(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    if (!m_pullProcess)
    {
        return;
    }
    QString currentTag;
    const auto args = m_pullProcess->arguments();
    if (args.size() >= 3)
    {
        currentTag = args.at(2);
    }
    const QString msg = m_pullProcess->errorString();
    m_pullProcess->deleteLater();
    m_pullProcess = nullptr;
    emit pullFailed(currentTag, msg);
}

void LocalModelManager::listModels()
{
    // 服务运行中：优先走 HTTP API
    if (m_serviceStatus == ServiceStatus::Running || m_serviceStatus == ServiceStatus::Unknown)
    {
        QNetworkRequest req(QUrl(ollamaBaseUrl() + "/api/tags"));
        req.setTransferTimeout(LIST_TIMEOUT_MS);
        QNetworkReply* reply = m_networkManager->get(req);
        connect(reply, &QNetworkReply::finished, this, &LocalModelManager::onListReplyFinished);
        return;
    }

    if (!isOllamaInstalled())
    {
        emit modelListReady({});
        return;
    }
    // 服务未运行：回退到 ollama list 子进程
    listModelsViaProcess();
}

void LocalModelManager::onListReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
    {
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        // HTTP 失败：尝试回退到子进程
        listModelsViaProcess();
        return;
    }

    QList<LocalModelInfo> models;
    const QByteArray data = reply->readAll();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
    {
        emit modelListReady(models);
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray arr = root.value("models").toArray();
    for (const QJsonValue& v : arr)
    {
        const QJsonObject m = v.toObject();
        LocalModelInfo info;
        info.tag = m.value("name").toString();
        info.name = info.tag.section(':', 0, 0);
        info.sizeBytes = m.value("size").toVariant().toLongLong();
        info.size = formatBytes(info.sizeBytes);
        info.modifiedAt = m.value("modified_at").toString();
        info.digest = m.value("digest").toString();
        if (!info.tag.isEmpty())
        {
            models.append(info);
        }
    }
    std::sort(models.begin(), models.end(),
              [](const LocalModelInfo& a, const LocalModelInfo& b) {
                  return a.name.toLower() < b.name.toLower();
              });
    emit modelListReady(models);
}

void LocalModelManager::listModelsViaProcess()
{
    const QString exe = ollamaPath();
    if (exe.isEmpty())
    {
        emit modelListReady({});
        return;
    }
    auto* proc = new QProcess(this);
    proc->setProgram(exe);
    proc->setArguments({"list"});
    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        QList<LocalModelInfo> models;
        if (exitCode != 0)
        {
            emit modelListReady(models);
            return;
        }
        const QStringList lines =
            QString::fromUtf8(proc->readAllStandardOutput())
                .split('\n', Qt::SkipEmptyParts);
        // ollama list 输出表头：NAME / ID / SIZE / MODIFIED
        for (int i = 1; i < lines.size(); ++i)
        {
            const QString line = lines.at(i).trimmed();
            if (line.isEmpty())
            {
                continue;
            }
            // 简化解析：按连续空格分列
            const QRegularExpression sep("\\s{2,}");
            const QStringList cols = line.split(sep, Qt::SkipEmptyParts);
            if (cols.size() < 3)
            {
                continue;
            }
            LocalModelInfo info;
            info.tag = cols.at(0);
            info.name = info.tag.section(':', 0, 0);
            info.size = cols.size() >= 3 ? cols.at(2) : QString();
            // modifiedAt 在第 4 列，若有
            if (cols.size() >= 4)
            {
                info.modifiedAt = cols.at(3);
            }
            if (!info.tag.isEmpty())
            {
                models.append(info);
            }
        }
        std::sort(models.begin(), models.end(),
                  [](const LocalModelInfo& a, const LocalModelInfo& b) {
                      return a.name.toLower() < b.name.toLower();
                  });
        emit modelListReady(models);
    });
    proc->start(QIODevice::ReadOnly);
    if (!proc->waitForStarted(2000))
    {
        proc->deleteLater();
        emit modelListReady({});
    }
}

void LocalModelManager::removeModel(const QString& tag)
{
    const QString exe = ollamaPath();
    if (exe.isEmpty())
    {
        emit modelRemoved(tag, false, tr("未找到 Ollama 可执行文件"));
        return;
    }
    if (m_removeProcess)
    {
        emit modelRemoved(tag, false, tr("已有删除任务进行中"));
        return;
    }
    m_removeTag = tag;
    m_removeProcess = new QProcess(this);
    m_removeProcess->setProgram(exe);
    m_removeProcess->setArguments({"rm", tag});
    connect(m_removeProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LocalModelManager::onRemoveFinished);
    m_removeProcess->start(QIODevice::ReadOnly);
    if (!m_removeProcess->waitForStarted(2000))
    {
        emit modelRemoved(tag, false, m_removeProcess->errorString());
        m_removeProcess->deleteLater();
        m_removeProcess = nullptr;
        m_removeTag.clear();
    }
}

void LocalModelManager::onRemoveFinished(int exitCode, QProcess::ExitStatus)
{
    const QString tag = m_removeTag;
    m_removeTag.clear();
    if (!m_removeProcess)
    {
        return;
    }
    const QString errOut =
        QString::fromUtf8(m_removeProcess->readAllStandardError()).trimmed();
    m_removeProcess->deleteLater();
    m_removeProcess = nullptr;
    if (exitCode == 0)
    {
        emit modelRemoved(tag, true, {});
    }
    else
    {
        emit modelRemoved(tag, false,
                          errOut.isEmpty() ? tr("退出码 %1").arg(exitCode) : errOut);
    }
}

QList<LocalModelPreset> LocalModelManager::presetModels() const
{
    return buildPresetModels();
}

QList<LocalModelPreset> LocalModelManager::buildPresetModels()
{
    return {
        // ===== Gemma 3 系列 =====
        {QStringLiteral("gemma3:1b"),  QStringLiteral("Gemma 3 1B"),
         QStringLiteral("Gemma"),  QStringLiteral("1B 参数，1GB 显存可跑，入门首选"),
         QStringLiteral("约 0.8 GB")},
        {QStringLiteral("gemma3:4b"),  QStringLiteral("Gemma 3 4B"),
         QStringLiteral("Gemma"),  QStringLiteral("4B 参数，4GB 显存推荐，速度与质量平衡"),
         QStringLiteral("约 2.5 GB")},
        {QStringLiteral("gemma3:12b"), QStringLiteral("Gemma 3 12B"),
         QStringLiteral("Gemma"),  QStringLiteral("12B 参数，需 8GB+ 显存，质量更佳"),
         QStringLiteral("约 7.4 GB")},
        {QStringLiteral("gemma3:27b"), QStringLiteral("Gemma 3 27B"),
         QStringLiteral("Gemma"),  QStringLiteral("27B 参数，需 16GB+ 显存，最高质量"),
         QStringLiteral("约 16 GB")},

        // ===== Qwen3 系列 =====
        {QStringLiteral("qwen3:0.6b"), QStringLiteral("Qwen3 0.6B"),
         QStringLiteral("Qwen"),   QStringLiteral("0.6B 参数，超轻量，集成显卡可跑"),
         QStringLiteral("约 0.5 GB")},
        {QStringLiteral("qwen3:1.7b"), QStringLiteral("Qwen3 1.7B"),
         QStringLiteral("Qwen"),   QStringLiteral("1.7B 参数，2GB 显存可用"),
         QStringLiteral("约 1.0 GB")},
        {QStringLiteral("qwen3:4b"),   QStringLiteral("Qwen3 4B"),
         QStringLiteral("Qwen"),   QStringLiteral("4B 参数，4GB 显存推荐，中文优秀"),
         QStringLiteral("约 2.5 GB")},
        {QStringLiteral("qwen3:8b"),   QStringLiteral("Qwen3 8B"),
         QStringLiteral("Qwen"),   QStringLiteral("8B 参数，6GB+ 显存推荐"),
         QStringLiteral("约 5.0 GB")},
        {QStringLiteral("qwen3:14b"),  QStringLiteral("Qwen3 14B"),
         QStringLiteral("Qwen"),   QStringLiteral("14B 参数，10GB+ 显存推荐"),
         QStringLiteral("约 8.8 GB")},
    };
}

// ============================================================================
// 安装器下载与运行
// ============================================================================

void LocalModelManager::downloadInstaller()
{
    if (m_installerDownloader)
    {
        return;
    }

    // 下载到软件同级 BlockBox 文件夹
    const QString dir = downloadDir();
    if (dir.isEmpty())
    {
        emit installerDownloadFailed(tr("无法定位下载目录"));
        return;
    }
    m_installerPath = QDir(dir).filePath("OllamaSetup.exe");

    // 在任务系统中注册一个下载任务，任务栏会自动显示进度卡片
    const QString fileName = QFileInfo(m_installerPath).fileName();
    const QString taskId = DownloadTaskManager::instance()->addTask(
        tr("Ollama 安装器"), dir, QString(), QStringList());
    m_installerTaskId = taskId;
    DownloadTaskManager::instance()->updateTaskStatus(
        taskId, DownloadTaskStatus::Downloading, tr("连接中..."));
    DownloadTaskManager::instance()->updateTaskCurrentFile(taskId, fileName);

    // 多源回退：依次尝试每个 URL，全部失败才算失败
    m_installerUrlIndex = 0;
    startInstallerDownloadWithCurrentUrl();
}

void LocalModelManager::startInstallerDownloadWithCurrentUrl()
{
    const QStringList urls = installerUrls();
    if (m_installerUrlIndex >= urls.size())
    {
        // 全部源都失败
        if (!m_installerTaskId.isEmpty())
        {
            DownloadTaskManager::instance()->updateTaskStatus(
                m_installerTaskId, DownloadTaskStatus::Failed,
                tr("所有下载源均失败"));
            m_installerTaskId.clear();
        }
        if (m_installerDownloader)
        {
            m_installerDownloader->deleteLater();
            m_installerDownloader = nullptr;
        }
        emit installerDownloadFailed(tr("所有下载源均失败"));
        return;
    }

    const QString url = urls.at(m_installerUrlIndex);
    // 切换源时清理上一次的 downloader
    if (m_installerDownloader)
    {
        m_installerDownloader->disconnect(this);
        m_installerDownloader->cancelDownload();
        m_installerDownloader->deleteLater();
    }
    m_installerDownloader = new MultiThreadDownloader(this);
    connect(m_installerDownloader, &MultiThreadDownloader::downloadProgress,
            this, &LocalModelManager::onInstallerProgress);
    connect(m_installerDownloader, &MultiThreadDownloader::downloadCompleted,
            this, &LocalModelManager::onInstallerCompleted);
    connect(m_installerDownloader, &MultiThreadDownloader::downloadFailed,
            this, &LocalModelManager::onInstallerFailed);

    if (!m_installerTaskId.isEmpty())
    {
        const QStringList urls = installerUrls();
        DownloadTaskManager::instance()->updateTaskStatus(
            m_installerTaskId, DownloadTaskStatus::Downloading,
            tr("正在从源 %1/%2 下载...").arg(m_installerUrlIndex + 1).arg(urls.size()));
    }

    m_installerDownloader->startDownload(
        url, m_installerPath, INSTALLER_THREAD_COUNT, m_installerTaskId,
        QStringLiteral("Mozilla/5.0 (BlockBox LocalModelManager)"));
}

void LocalModelManager::cancelInstallerDownload()
{
    if (!m_installerDownloader)
    {
        return;
    }
    m_installerDownloader->cancelDownload();
    m_installerDownloader->deleteLater();
    m_installerDownloader = nullptr;
    if (!m_installerTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskStatus(
            m_installerTaskId, DownloadTaskStatus::Cancelled, tr("用户已取消"));
        m_installerTaskId.clear();
    }
    emit installerDownloadFailed(tr("用户已取消下载"));
}

void LocalModelManager::onInstallerProgress(const QString& taskId, qint64 received, qint64 total)
{
    Q_UNUSED(taskId)
    int percent = -1;
    if (total > 0)
    {
        percent = static_cast<int>((received * 100) / total);
    }
    emit installerDownloadProgress(percent, received, total);
}

void LocalModelManager::onInstallerCompleted(const QString& taskId, const QString& savePath)
{
    Q_UNUSED(taskId)
    if (m_installerDownloader)
    {
        m_installerDownloader->deleteLater();
        m_installerDownloader = nullptr;
    }
    m_installerPath = savePath;
    if (!m_installerTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_installerTaskId, 100);
        DownloadTaskManager::instance()->updateTaskStatus(
            m_installerTaskId, DownloadTaskStatus::Completed, tr("下载完成"));
        m_installerTaskId.clear();
    }
    emit installerDownloaded(savePath);
}

void LocalModelManager::onInstallerFailed(const QString& taskId, const QString& error)
{
    Q_UNUSED(taskId)
    // 当前源失败，尝试下一个源
    ++m_installerUrlIndex;
    const QStringList urls = installerUrls();
    if (m_installerUrlIndex < urls.size())
    {
        // 切换到下一个源继续下载
        startInstallerDownloadWithCurrentUrl();
        return;
    }

    // 全部源都失败
    if (m_installerDownloader)
    {
        m_installerDownloader->deleteLater();
        m_installerDownloader = nullptr;
    }
    if (!m_installerTaskId.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskStatus(
            m_installerTaskId, DownloadTaskStatus::Failed, error);
        m_installerTaskId.clear();
    }
    emit installerDownloadFailed(error);
}

bool LocalModelManager::runInstaller()
{
    if (m_installerPath.isEmpty() || !QFileInfo::exists(m_installerPath))
    {
        return false;
    }
    // startDetached 会让 UAC 弹出，安装器自行完成安装与服务注册
    return QProcess::startDetached(m_installerPath, {});
}

// ============================================================================
// 工具方法
// ============================================================================

QString LocalModelManager::ollamaBaseUrl()
{
    return QStringLiteral("http://127.0.0.1:%1").arg(OLLAMA_PORT);
}

QString LocalModelManager::ollamaOpenAiUrl()
{
    return ollamaBaseUrl() + "/v1";
}

QString LocalModelManager::downloadDir()
{
    // 软件同级 BlockBox 文件夹：<应用所在目录>/BlockBox/
    const QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.isEmpty())
    {
        return QString();
    }
    const QString dir = QDir(appDir).absoluteFilePath("BlockBox");
    QDir().mkpath(dir); // 不存在则创建
    return dir;
}

const QStringList LocalModelManager::installerUrls()
{
    // 多源回退列表：官方源优先，GitHub Releases 次之，镜像源兜底
    return {
        QStringLiteral("https://ollama.com/download/OllamaSetup.exe"),
        QStringLiteral("https://github.com/ollama/ollama/releases/latest/download/OllamaSetup.exe"),
        QStringLiteral("https://mirror.ghproxy.com/https://github.com/ollama/ollama/releases/latest/download/OllamaSetup.exe"),
        QStringLiteral("https://ghfast.top/https://github.com/ollama/ollama/releases/latest/download/OllamaSetup.exe"),
    };
}

QString LocalModelManager::formatBytes(qint64 bytes)
{
    if (bytes < 1024)
    {
        return QStringLiteral("%1 B").arg(bytes);
    }
    double v = static_cast<double>(bytes);
    static const QStringList units = {"KB", "MB", "GB", "TB"};
    int unitIdx = -1;
    while (v >= 1024.0 && unitIdx < units.size() - 1)
    {
        v /= 1024.0;
        ++unitIdx;
    }
    return QStringLiteral("%1 %2").arg(v, 0, 'f', 2).arg(units.value(unitIdx));
}
