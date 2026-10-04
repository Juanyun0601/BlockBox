/**
 * @file   ModNameFetcher.cpp
 * @brief  模组中文名/英文名获取协调器实现
 * @author BlockBox Team
 * @date   2026-06-19
 */
#include "ModNameFetcher.h"
#include "MCModAPI.h"
#include "ModrinthAPI.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

ModNameFetcher::ModNameFetcher(QObject *parent)
    : QObject(parent)
    , m_mcmodApi(new MCModAPI(this))
    , m_modrinthApi(new ModrinthAPI(this))
{
    connect(m_mcmodApi, &MCModAPI::nameResolved,
            this, &ModNameFetcher::onMcmodNameResolved);
    connect(m_mcmodApi, &MCModAPI::lookupFailed,
            this, &ModNameFetcher::onMcmodLookupFailed);
    connect(m_modrinthApi, &ModrinthAPI::modDetailReceived,
            this, &ModNameFetcher::onModrinthDetailReceived);
    connect(m_modrinthApi, &ModrinthAPI::modDetailFailed,
            this, &ModNameFetcher::onModrinthDetailFailed);

    loadPersistentCache();
}

ModNameFetcher::~ModNameFetcher()
{
    savePersistentCache();
}

void ModNameFetcher::fetchNames(const ModInfo &modInfo)
{
    QList<ModInfo> list;
    list.append(modInfo);
    fetchNamesBatch(list, m_maxConcurrent);
}

void ModNameFetcher::fetchNamesBatch(const QList<ModInfo> &modList, int maxConcurrent)
{
    m_maxConcurrent = qBound(1, maxConcurrent, 8);

    for (const ModInfo &mod : modList)
    {
        QString key = mod.id.isEmpty() ? mod.fileName : mod.id;
        if (key.isEmpty())
            continue;

        // 跳过已提交的
        if (m_submitted.contains(key))
            continue;
        m_submitted.insert(key);

        // 检查缓存
        if (m_cache.contains(key))
        {
            NameCache cached = m_cache.value(key);
            ModInfo cachedInfo = mod;
            cachedInfo.chineseName = cached.chineseName;
            cachedInfo.englishName = cached.englishName;
            cachedInfo.mcmodUrl = cached.mcmodUrl;
            emit namesResolved(cachedInfo);
            continue;
        }

        m_queue.enqueue(mod);
    }

    m_totalCount = m_queue.size() + m_resolvedCount + m_failedCount;
    if (m_totalCount == 0)
    {
        emit batchCompleted(0, 0);
        return;
    }

    processQueue();
}

void ModNameFetcher::clearCache()
{
    m_cache.clear();
    m_submitted.clear();
    m_queue.clear();
    m_pendingMods.clear();
    m_inFlight = 0;
    m_resolvedCount = 0;
    m_failedCount = 0;
    m_totalCount = 0;
}

bool ModNameFetcher::isBusy() const
{
    return m_inFlight > 0 || !m_queue.isEmpty();
}

void ModNameFetcher::processQueue()
{
    while (m_inFlight < m_maxConcurrent && !m_queue.isEmpty())
    {
        ModInfo mod = m_queue.dequeue();
        QString key = mod.id.isEmpty() ? mod.fileName : mod.id;

        PendingMod pending;
        pending.info = mod;
        pending.modrinthDone = false;
        pending.mcmodDone = false;

        // 检查是否已经有值，不需要再查
        if (!mod.chineseName.isEmpty())
            pending.mcmodDone = true;
        if (!mod.englishName.isEmpty() || mod.id.isEmpty())
            pending.modrinthDone = true;

        m_pendingMods.insert(key, pending);
        m_inFlight++;

        resolveMod(pending);
    }

    // 如果队列空了且没有在途请求，输出统计
    if (m_queue.isEmpty() && m_inFlight == 0)
    {
        // 可能还有已经发出但未完成的请求（在 MCModAPI/ModrinthAPI 内部的连接中）
        // batchCompleted 会在每个 pending 完成后检查是否全部完成
    }
}

void ModNameFetcher::resolveMod(PendingMod &pending)
{
    const ModInfo &mod = pending.info;
    QString key = mod.id.isEmpty() ? mod.fileName : mod.id;

    // 如果需要查询中文名且还未完成
    if (!pending.mcmodDone && mod.chineseName.isEmpty())
    {
        // 使用已有的英文名（来自 metadata）查询中文名
        // 如果 mod.name 是英文名，直接用它查
        // 如果 mod.name 已经是中文名（罕见），用 mod.id 查
        QString searchName = mod.englishName.isEmpty() ? mod.name : mod.englishName;
        if (!searchName.isEmpty())
        {
            m_mcmodApi->lookupChineseName(key, searchName);
        }
        else
        {
            pending.mcmodDone = true;
        }
    }

    // 如果需要查询英文名且还未完成
    if (!pending.modrinthDone)
    {
        if (!mod.id.isEmpty())
        {
            // 用 Modrinth API 获取项目详情（会返回 title 作为英文名）
            m_modrinthApi->fetchModDetail(mod.id);
        }
        else
        {
            pending.modrinthDone = true;
        }
    }

    // 如果两个都不需要查，直接返回
    if (pending.mcmodDone && pending.modrinthDone)
    {
        tryEmitResult(key);
    }
}

void ModNameFetcher::tryEmitResult(const QString &key)
{
    if (!m_pendingMods.contains(key))
        return;

    PendingMod pending = m_pendingMods.take(key);
    m_inFlight--;

    // 更新缓存
    NameCache cached;
    cached.chineseName = pending.info.chineseName;
    cached.englishName = pending.info.englishName;
    cached.mcmodUrl = pending.info.mcmodUrl;
    m_cache.insert(key, cached);

    // 每次成功解析后保存持久化缓存
    savePersistentCache();

    // 判断是否至少有一个名称被解析到
    bool hasAnyName = !pending.info.chineseName.isEmpty()
                      || !pending.info.englishName.isEmpty();

    if (hasAnyName)
    {
        m_resolvedCount++;
        emit namesResolved(pending.info);
    }
    else
    {
        m_failedCount++;
        QString englishName = pending.info.englishName.isEmpty()
                              ? pending.info.name : pending.info.englishName;
        emit namesFetchFailed(pending.info.id, englishName);
    }

    emit batchProgress(m_resolvedCount, m_totalCount);

    // 处理队列中的下一个
    processQueue();

    // 检查是否全部完成
    if (m_queue.isEmpty() && m_inFlight == 0)
    {
        emit batchCompleted(m_resolvedCount, m_failedCount);
    }
}

// ---- MCModAPI callbacks ----

void ModNameFetcher::onMcmodNameResolved(const QString &modId, const QString &chineseName, const QString &mcmodUrl)
{
    // 从 key 查找 pending mod
    QString key = modId;
    if (!m_pendingMods.contains(key))
    {
        // 可能是 id 和实际 key 不一致，尝试用完整 map 查找
        for (auto it = m_pendingMods.begin(); it != m_pendingMods.end(); ++it)
        {
            if (it->info.id == modId)
            {
                key = it.key();
                break;
            }
        }
        if (!m_pendingMods.contains(key))
            return;
    }

    PendingMod &pending = m_pendingMods[key];
    pending.info.chineseName = chineseName;
    pending.info.mcmodUrl = mcmodUrl;
    pending.mcmodDone = true;

    if (pending.modrinthDone)
        tryEmitResult(key);
}

void ModNameFetcher::onMcmodLookupFailed(const QString &modId, const QString &englishName)
{
    Q_UNUSED(englishName);
    QString key = modId;
    if (!m_pendingMods.contains(key))
    {
        for (auto it = m_pendingMods.begin(); it != m_pendingMods.end(); ++it)
        {
            if (it->info.id == modId)
            {
                key = it.key();
                break;
            }
        }
        if (!m_pendingMods.contains(key))
            return;
    }

    PendingMod &pending = m_pendingMods[key];
    pending.mcmodDone = true;

    if (pending.modrinthDone)
        tryEmitResult(key);
}

// ---- ModrinthAPI callbacks ----

void ModNameFetcher::onModrinthDetailReceived(const ModInfo &detail)
{
    QString key = detail.id;
    if (!m_pendingMods.contains(key))
        return;

    PendingMod &pending = m_pendingMods[key];

    // 从 Modrinth 获取官方英文名（title 字段）
    if (!detail.name.isEmpty())
    {
        pending.info.englishName = detail.name;
    }

    // 补充其他信息（如果有比本地更好的数据）
    if (pending.info.description.isEmpty() && !detail.description.isEmpty())
        pending.info.description = detail.description;
    if (pending.info.iconUrl.isEmpty() && !detail.iconUrl.isEmpty())
        pending.info.iconUrl = detail.iconUrl;
    if (pending.info.pageUrl.isEmpty() && !detail.pageUrl.isEmpty())
        pending.info.pageUrl = detail.pageUrl;
    if (pending.info.downloadCount <= 0 && detail.downloadCount > 0)
        pending.info.downloadCount = detail.downloadCount;

    pending.modrinthDone = true;

    if (pending.mcmodDone)
        tryEmitResult(key);
}

void ModNameFetcher::onModrinthDetailFailed(const QString &error)
{
    Q_UNUSED(error);
    // 在所有 pending 中查找哪个在等待 Modrinth
    for (auto it = m_pendingMods.begin(); it != m_pendingMods.end(); ++it)
    {
        PendingMod &pending = it.value();
        if (!pending.modrinthDone)
        {
            pending.modrinthDone = true;

            if (pending.mcmodDone)
            {
                QString key = it.key();
                tryEmitResult(key);
                return;
            }
        }
    }
}

QString ModNameFetcher::cacheFilePath() const
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists())
        dir.mkpath(".");
    return dir.filePath("mod_name_cache.json");
}

void ModNameFetcher::loadPersistentCache()
{
    QFile file(cacheFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return;

    QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        QJsonObject entry = it.value().toObject();
        NameCache cached;
        cached.chineseName = entry["chineseName"].toString();
        cached.englishName = entry["englishName"].toString();
        cached.mcmodUrl = entry["mcmodUrl"].toString();
        m_cache.insert(it.key(), cached);
    }
}

void ModNameFetcher::savePersistentCache()
{
    QJsonObject root;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
    {
        QJsonObject entry;
        entry["chineseName"] = it.value().chineseName;
        entry["englishName"] = it.value().englishName;
        entry["mcmodUrl"] = it.value().mcmodUrl;
        root.insert(it.key(), entry);
    }

    QFile file(cacheFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    QJsonDocument doc(root);
    file.write(doc.toJson());
    file.close();
}