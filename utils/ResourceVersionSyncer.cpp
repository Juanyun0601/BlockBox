#include "ResourceVersionSyncer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

#include "utils/DownloadTaskManager.h"
#include "utils/McimHelper.h"
#include "utils/content/ContentDownloader.h"
#include "utils/mod/ModrinthAPI.h"

namespace {

/** 提取一批资源的 SHA-1 列表（去重） */
QList<QString> sha1List(const QList<ResourceSyncItem> &items)
{
    QList<QString> sha1s;
    for (const ResourceSyncItem &item : items)
    {
        if (!item.sha1.isEmpty() && !sha1s.contains(item.sha1))
            sha1s.append(item.sha1);
    }
    return sha1s;
}

/** 参与同步的资源目录（类型 → 实例内相对目录） */
struct SyncFolder
{
    ContentType type;
    const char *folder;
};

/** 从一段文本里嗅探加载器名（Forge / Fabric / NeoForge / Quilt，无则空） */
QString detectLoaderFromText(const QString &text)
{
    const QString lower = text.toLower();
    if (lower.contains("neoforge"))
        return QStringLiteral("NeoForge");
    if (lower.contains("forge"))
        return QStringLiteral("Forge");
    if (lower.contains("fabric"))
        return QStringLiteral("Fabric");
    if (lower.contains("quilt"))
        return QStringLiteral("Quilt");
    return QString();
}

/** 从实例版本 JSON 读取加载器（与实例管理页的检测逻辑一致） */
QString detectInstanceLoader(const QString &instancePath)
{
    const QString instanceName = QFileInfo(instancePath).fileName();
    QString jsonPath = instancePath + "/" + instanceName + ".json";
    if (!QFile::exists(jsonPath))
        jsonPath = instancePath + "/version.json";

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
    {
        const QString loader = detectLoaderFromText(it.key());
        if (!loader.isEmpty())
            return loader;
        if (it.value().isString())
        {
            const QString valueLoader = detectLoaderFromText(it.value().toString());
            if (!valueLoader.isEmpty())
                return valueLoader;
        }
    }
    QString loader = detectLoaderFromText(root.value("inheritsFrom").toString());
    if (loader.isEmpty())
        loader = detectLoaderFromText(root.value("id").toString());
    return loader;
}

/** 收集实例里三类资源的文件清单（不读内容，只列路径与类型） */
QList<ResourceSyncItem> collectSyncItems(const QString &instancePath)
{
    static const QList<SyncFolder> kFolders = {
        { ContentType::Mod,          "mods" },
        { ContentType::ResourcePack, "resourcepacks" },
        { ContentType::ShaderPack,   "shaderpacks" },
    };

    QList<ResourceSyncItem> items;
    for (const SyncFolder &folder : kFolders)
    {
        QDir dir(instancePath + "/" + QLatin1String(folder.folder));
        if (!dir.exists())
            continue;

        // 模组可能是禁用状态（"xxx.jar.disabled"），资源包 / 光影包只认 zip
        const QStringList nameFilters = (folder.type == ContentType::Mod)
            ? QStringList{ QStringLiteral("*.jar"), QStringLiteral("*.jar.disabled") }
            : QStringList{ QStringLiteral("*.zip") };

        const QFileInfoList entries = dir.entryInfoList(nameFilters, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : entries)
        {
            ResourceSyncItem item;
            item.filePath = fi.absoluteFilePath();
            item.fileName = fi.fileName();
            item.displayName = fi.fileName();
            item.type = folder.type;
            item.enabled = true;
            if (folder.type == ContentType::Mod && item.displayName.endsWith(
                    QStringLiteral(".disabled"), Qt::CaseInsensitive))
            {
                item.displayName.chop(strlen(".disabled"));
                item.enabled = false;
            }
            items.append(item);
        }
    }
    return items;
}

/** 分块读取文件计算 SHA-1（大文件不整块进内存），失败返回空串 */
QString computeSha1(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hasher(QCryptographicHash::Sha1);
    char buffer[64 * 1024];
    while (!file.atEnd())
    {
        const qint64 read = file.read(buffer, sizeof(buffer));
        if (read <= 0)
            return QString();
        hasher.addData(QByteArrayView(buffer, read));
    }
    return QString::fromLatin1(hasher.result().toHex());
}

} // namespace

ResourceVersionSyncer::ResourceVersionSyncer(QObject *parent)
    : QObject(parent)
{
}

ResourceVersionSyncer::~ResourceVersionSyncer()
{
}

void ResourceVersionSyncer::start(const QString &instancePath,
                                  const QString &oldVersion, const QString &newVersion)
{
    Q_UNUSED(oldVersion);
    if (m_running || instancePath.isEmpty() || newVersion.isEmpty())
        return;

    m_running = true;
    m_instancePath = instancePath;
    m_newVersion = newVersion;
    m_loader = detectInstanceLoader(instancePath);
    m_converted.clear();
    m_pending.clear();
    m_downloadQueue.clear();
    m_downloadDone = 0;
    m_taskId.clear();
    m_modsCheckDone = false;
    m_othersCheckDone = false;

    const QList<ResourceSyncItem> collected = collectSyncItems(instancePath);
    if (collected.isEmpty())
    {
        m_running = false;
        emit finished(m_converted, m_pending);
        return;
    }

    // 哈希计算放后台线程：模组可能有几百个，逐个读文件不能占住 GUI 线程
    auto *watcher = new QFutureWatcher<QList<ResourceSyncItem>>(this);
    connect(watcher, &QFutureWatcher<QList<ResourceSyncItem>>::finished, this,
            [this, watcher]()
    {
        watcher->deleteLater();
        m_items = watcher->result();
        // 哈希算不出来的文件没法识别，也不便替换，保留原地不动、不进清单
        QList<ResourceSyncItem> hashable;
        for (const ResourceSyncItem &item : std::as_const(m_items))
        {
            if (!item.sha1.isEmpty())
                hashable.append(item);
        }
        m_items = hashable;
        if (m_items.isEmpty())
        {
            finish();
            return;
        }
        startMatch();
    });
    // 交给后台线程的是去掉 const 的拷贝：mutable lambda 里要就地写哈希，
    // 而按值捕获 const 对象得到的成员仍是 const（GCC 会拒绝改写）
    QList<ResourceSyncItem> items = collected;
    watcher->setFuture(QtConcurrent::run([items]() mutable -> QList<ResourceSyncItem>
    {
        for (ResourceSyncItem &item : items)
            item.sha1 = computeSha1(item.filePath);
        return items;
    }));
}

void ResourceVersionSyncer::startMatch()
{
    if (!m_matchApi)
    {
        m_matchApi = new ModrinthAPI(this);
        m_matchApi->setBaseUrl(McimHelper::modrinthApiBaseUrl());
        connect(m_matchApi, &ModrinthAPI::hashesMatched, this,
                &ResourceVersionSyncer::onHashesMatched);
        connect(m_matchApi, &ModrinthAPI::hashesMatchFailed, this,
                [this](const QString &error)
        {
            // 指纹匹配失败：本轮什么都不改，避免误判资源为不兼容
            m_running = false;
            emit checkFailed(error);
        });
    }

    QList<QString> sha1s;
    for (const ResourceSyncItem &item : std::as_const(m_items))
        sha1s.append(item.sha1);

    if (sha1s.isEmpty())
    {
        finish();
        return;
    }
    m_matchApi->matchHashes(sha1s);
}

void ResourceVersionSyncer::onHashesMatched(const QMap<QString, ModVersionFile> &versions)
{
    // 待查询目标版本的资源分两桶：模组要按实例加载器过滤，资源包 / 光影包不过滤
    QList<QString> modSha1s;
    QList<QString> otherSha1s;
    m_modCheckItems.clear();
    m_otherCheckItems.clear();

    for (ResourceSyncItem item : std::as_const(m_items))
    {
        if (!versions.contains(item.sha1))
        {
            // 指纹认不出的本地资源（自制 / 小众资源等）：交给用户决定去留
            item.identified = false;
            item.note = tr("无法识别资源来源，未确认是否兼容 %1").arg(m_newVersion);
            m_pending.append(item);
            continue;
        }

        const ModVersionFile installed = versions.value(item.sha1);
        item.identified = true;
        item.installedVersion = installed.version;
        item.gameVersions = installed.gameVersions;

        if (installed.gameVersions.contains(m_newVersion))
        {
            // 当前文件本身就支持新版本，无需转化
            continue;
        }

        if (item.type == ContentType::Mod)
        {
            m_modCheckItems.append(item);
            modSha1s.append(item.sha1);
        }
        else
        {
            m_otherCheckItems.append(item);
            otherSha1s.append(item.sha1);
        }
    }

    startUpdateChecks();
}

void ResourceVersionSyncer::startUpdateChecks()
{
    if (!m_modsApi)
    {
        m_modsApi = new ModrinthAPI(this);
        m_modsApi->setBaseUrl(McimHelper::modrinthApiBaseUrl());
        connect(m_modsApi, &ModrinthAPI::updatesChecked, this,
                [this](const QMap<QString, ModVersionFile> &versions)
        {
            onUpdatesChecked(versions, true);
        });
        connect(m_modsApi, &ModrinthAPI::updatesCheckFailed, this,
                [this](const QString &)
        {
            onUpdatesChecked(QMap<QString, ModVersionFile>(), true);
        });
    }
    if (!m_othersApi)
    {
        m_othersApi = new ModrinthAPI(this);
        m_othersApi->setBaseUrl(McimHelper::modrinthApiBaseUrl());
        connect(m_othersApi, &ModrinthAPI::updatesChecked, this,
                [this](const QMap<QString, ModVersionFile> &versions)
        {
            onUpdatesChecked(versions, false);
        });
        connect(m_othersApi, &ModrinthAPI::updatesCheckFailed, this,
                [this](const QString &)
        {
            onUpdatesChecked(QMap<QString, ModVersionFile>(), false);
        });
    }

    if (m_modCheckItems.isEmpty())
        m_modsCheckDone = true;
    else
        m_modsApi->checkModUpdates(sha1List(m_modCheckItems),
                                   QStringList{ m_newVersion },
                                   m_loader.isEmpty() ? QStringList()
                                                      : QStringList{ m_loader });

    if (m_otherCheckItems.isEmpty())
        m_othersCheckDone = true;
    else
        m_othersApi->checkModUpdates(sha1List(m_otherCheckItems),
                                     QStringList{ m_newVersion },
                                     QStringList());

    // 两批都无需查询（全部已兼容 / 全部未识别）时直接进入收尾
    if (m_modsCheckDone && m_othersCheckDone)
        startNextDownload();
}

void ResourceVersionSyncer::onUpdatesChecked(const QMap<QString, ModVersionFile> &versions,
                                             bool forMods)
{
    QList<ResourceSyncItem> &checkItems = forMods ? m_modCheckItems : m_otherCheckItems;

    for (const ResourceSyncItem &item : std::as_const(checkItems))
    {
        if (!versions.contains(item.sha1))
        {
            // Modrinth 上没有支持新版本的版本文件 → 进待处理清单
            ResourceSyncItem pendingItem = item;
            pendingItem.note = tr("资源站上没有支持 %1 的版本").arg(m_newVersion);
            m_pending.append(pendingItem);
            continue;
        }

        const ModVersionFile target = versions.value(item.sha1);
        if (target.downloadUrl.isEmpty())
        {
            ResourceSyncItem pendingItem = item;
            pendingItem.note = tr("资源站上没有支持 %1 的版本").arg(m_newVersion);
            m_pending.append(pendingItem);
            continue;
        }

        DownloadJob job;
        job.item = item;
        job.target = target;
        m_downloadQueue.append(job);
    }
    checkItems.clear();

    if (forMods)
        m_modsCheckDone = true;
    else
        m_othersCheckDone = true;
    if (m_modsCheckDone && m_othersCheckDone)
        startNextDownload();
}

void ResourceVersionSyncer::startNextDownload()
{
    if (m_downloadQueue.isEmpty())
    {
        finish();
        return;
    }

    const DownloadJob job = m_downloadQueue.takeFirst();
    m_activeJob = job;

    // 目标文件名：优先用资源站返回的文件名，取不到时退回 URL 文件名 / 原文件名
    QString fileName = job.target.fileName;
    if (fileName.isEmpty())
    {
        fileName = QUrl(job.target.downloadUrl).fileName();
        if (fileName.isEmpty() || fileName.contains('?'))
            fileName = job.item.displayName;
    }

    if (m_taskId.isEmpty())
    {
        const QString instanceName = QFileInfo(m_instancePath).fileName();
        m_taskId = DownloadTaskManager::instance()->addTaskWithType(
            instanceName, m_instancePath, QString(), QStringList(),
            TaskType::ContentDownload);
        DownloadTaskManager::instance()->updateTaskStatus(
            m_taskId, DownloadTaskStatus::Downloading, tr("同步资源版本到 %1").arg(m_newVersion));
    }
    DownloadTaskManager::instance()->updateTaskCurrentFile(m_taskId, fileName);
    DownloadTaskManager::instance()->updateTaskStage(m_taskId, DownloadStage::ClientJar, 0);

    // 镜像优先下载，失败自动回退官方源
    const QString requestUrl = McimHelper::rewriteFileUrl(job.target.downloadUrl);
    downloadJobFile(requestUrl, fileName, requestUrl != job.target.downloadUrl);
}

void ResourceVersionSyncer::downloadJobFile(const QString &url, const QString &fileName,
                                            bool allowFallback)
{
    auto *downloader = new ContentDownloader(this);
    m_activeDownloader = downloader;

    connect(downloader, &ContentDownloader::downloadProgress, this,
            [this, fileName](qint64 received, qint64 total)
    {
        DownloadTaskManager::instance()->updateTaskFileProgress(m_taskId, fileName, received, total);
        if (total > 0)
        {
            const int filePct = qBound(0, static_cast<int>(received * 100 / total), 100);
            const int overall = (m_downloadDone * 100 + filePct)
                / (m_downloadDone + m_downloadQueue.size() + 1);
            DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId,
                qBound(0, overall, 100));
        }
    });

    connect(downloader, &ContentDownloader::downloadFinished, this,
            [this, downloader, fileName](const QString &destPath)
    {
        downloader->deleteLater();
        m_activeDownloader = nullptr;

        ResourceSyncItem done = m_activeJob.item;
        finalizeReplacedItem(done, destPath);
        m_converted.append(done);
        m_downloadDone++;
        startNextDownload();
    });

    connect(downloader, &ContentDownloader::downloadFailed, this,
            [this, downloader, fileName, allowFallback](const QString &error)
    {
        Q_UNUSED(error);
        downloader->deleteLater();
        m_activeDownloader = nullptr;

        if (allowFallback)
        {
            downloadJobFile(m_activeJob.target.downloadUrl, fileName, false);
            return;
        }
        // 下载失败：保留原文件，进待处理清单让用户决定
        ResourceSyncItem pendingItem = m_activeJob.item;
        pendingItem.note = tr("适配版本下载失败，已保留原文件");
        m_pending.append(pendingItem);
        m_downloadDone++;
        startNextDownload();
    });

    downloader->downloadToInstance(url, fileName, m_instancePath, m_activeJob.item.type);
}

void ResourceVersionSyncer::finalizeReplacedItem(ResourceSyncItem &item, const QString &destPath)
{
    // 删除旧文件；与下载落地的同一路径时跳过（新文件已原地覆盖）
    if (!item.filePath.isEmpty()
        && QFileInfo(item.filePath).absoluteFilePath() != QFileInfo(destPath).absoluteFilePath())
    {
        QFile::remove(item.filePath);
    }

    // 禁用状态的模组转化后保持禁用：新文件补回 .disabled 后缀
    if (!item.enabled)
    {
        const QString disabledPath = destPath + QStringLiteral(".disabled");
        QFile::rename(destPath, disabledPath);
        item.filePath = disabledPath;
    }
    else
    {
        item.filePath = destPath;
    }
    item.fileName = QFileInfo(item.filePath).fileName();
    item.note.clear();
}

void ResourceVersionSyncer::finish()
{
    m_running = false;

    if (!m_taskId.isEmpty())
    {
        DownloadTaskManager::instance()->updateTaskStage(m_taskId, DownloadStage::Completed, 100);
        DownloadTaskManager::instance()->updateTaskProgressPercent(m_taskId, 100);
        DownloadTaskManager::instance()->updateTaskStatus(
            m_taskId, DownloadTaskStatus::Completed, tr("资源版本同步完成"));
    }

    emit finished(m_converted, m_pending);

    m_items.clear();
    m_converted.clear();
    m_pending.clear();
    m_downloadQueue.clear();
    m_modCheckItems.clear();
    m_otherCheckItems.clear();
}
