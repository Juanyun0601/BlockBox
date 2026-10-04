#include "ModLinkResolver.h"
#include "CurseForgeAPI.h"
#include "ModrinthAPI.h"
#include "MCModAPI.h"
#include "../SettingsManager.h"

#include <QDebug>
#include <QUrl>

ModLinkResolver::ModLinkResolver(QObject *parent)
    : QObject(parent)
    , m_cfApi(new CurseForgeAPI(this))
    , m_mrApi(new ModrinthAPI(this))
    , m_mcApi(new MCModAPI(this))
{
    QString apiKey = SettingsManager::instance()->property("curseforge_api_key").toString();
    if (!apiKey.isEmpty())
        m_cfApi->setApiKey(apiKey);

    // CurseForge: fingerprint first, then name search fallback
    connect(m_cfApi, &CurseForgeAPI::fingerprintCompleted,
            this, &ModLinkResolver::onCfFingerprintCompleted);
    connect(m_cfApi, &CurseForgeAPI::fingerprintFailed,
            this, &ModLinkResolver::onCfFingerprintFailed);
    connect(m_cfApi, &CurseForgeAPI::searchCompleted,
            this, &ModLinkResolver::onCfSearchCompleted);
    connect(m_cfApi, &CurseForgeAPI::searchFailed,
            this, &ModLinkResolver::onCfSearchFailed);

    // Modrinth: hash match first, then name search fallback
    connect(m_mrApi, &ModrinthAPI::hashMatched,
            this, &ModLinkResolver::onMrHashMatched);
    connect(m_mrApi, &ModrinthAPI::hashMatchFailed,
            this, &ModLinkResolver::onMrHashMatchFailed);
    connect(m_mrApi, &ModrinthAPI::modDetailReceived,
            this, &ModLinkResolver::onMrDetailReceived);
    connect(m_mrApi, &ModrinthAPI::modDetailFailed,
            this, &ModLinkResolver::onMrDetailFailed);
    connect(m_mrApi, &ModrinthAPI::searchCompleted,
            this, &ModLinkResolver::onMrSearchCompleted);
    connect(m_mrApi, &ModrinthAPI::searchFailed,
            this, &ModLinkResolver::onMrSearchFailed);

    connect(m_mcApi, &MCModAPI::nameResolved,
            this, &ModLinkResolver::onMcmodNameResolved);
    connect(m_mcApi, &MCModAPI::lookupFailed,
            this, &ModLinkResolver::onMcmodLookupFailed);
}

ModLinkResolver::~ModLinkResolver()
{
}

void ModLinkResolver::resolveLinks(const ModInfo &modInfo)
{
    QString key = modInfo.id.isEmpty() ? modInfo.fileName : modInfo.id;
    if (key.isEmpty())
        return;

    m_queue.enqueue(modInfo);
    processQueue();
}

bool ModLinkResolver::isBusy() const
{
    return m_inFlight > 0 || !m_queue.isEmpty();
}

void ModLinkResolver::processQueue()
{
    while (m_inFlight < kMaxConcurrent && !m_queue.isEmpty())
    {
        ModInfo mod = m_queue.dequeue();
        QString key = mod.id.isEmpty() ? mod.fileName : mod.id;

        PendingLink pending;
        pending.info = mod;

        m_pendingLinks.insert(key, pending);
        m_inFlight++;

        // 搜索用名称：优先英文名，其次 name（永远不用中文名搜索 CF/MR）
        QString searchName = mod.englishName.isEmpty()
            ? mod.name
            : mod.englishName;

        // === CurseForge: 先试指纹匹配，无哈希时直接名称搜索 ===
        if (mod.curseforgeHash > 0) {
            pending.cfHashTried = true;
            m_cfFingerprintToKey.insert(mod.curseforgeHash, key);
            m_pendingLinks[key] = pending;
            m_cfApi->fingerprintMod(mod.curseforgeHash);
        } else {
            m_cfApi->searchMods(searchName, QString(), QString(), 0, 1, "popularity");
        }

        // === Modrinth: 先试哈希匹配，无 SHA-1 时直接名称搜索 ===
        if (!mod.sha1Hash.isEmpty()) {
            pending.mrHashTried = true;
            m_mrSha1ToKey.insert(mod.sha1Hash, key);
            m_pendingLinks[key] = pending;
            m_mrApi->matchHash(mod.sha1Hash);
        } else {
            m_mrApi->searchMods(searchName, QString(), QString(), 0, 1, "relevance");
        }

        // === MC百科：始终名称查找 ===
        m_mcApi->lookupChineseName(key, searchName);
    }
}

// ============================================================
// CurseForge 指纹匹配回调
// ============================================================

void ModLinkResolver::onCfFingerprintCompleted(quint32 fingerprint, const QString &modId, const QString &pageUrl)
{
    if (!m_cfFingerprintToKey.contains(fingerprint))
        return;

    QString key = m_cfFingerprintToKey.take(fingerprint);
    if (!m_pendingLinks.contains(key))
        return;

    PendingLink &pending = m_pendingLinks[key];
    pending.info.curseforgeUrl = pageUrl;
    pending.info.id = modId;
    pending.info.source = "curseforge";
    pending.cfDone = true;

    emit linksResolved(pending.info);
    tryFinishMod(key);
}

void ModLinkResolver::onCfFingerprintFailed(const QString &error)
{
    Q_UNUSED(error);
    // 清理所有待处理的指纹映射，并退回到名称搜索
    QList<QString> pendingKeys;
    for (auto it = m_cfFingerprintToKey.begin(); it != m_cfFingerprintToKey.end(); ++it)
        pendingKeys.append(it.value());
    m_cfFingerprintToKey.clear();

    for (const QString &key : pendingKeys)
    {
        if (!m_pendingLinks.contains(key))
            continue;
        PendingLink &pending = m_pendingLinks[key];
        if (!pending.cfDone)
        {
            QString searchName = pending.info.englishName.isEmpty()
                ? pending.info.name
                : pending.info.englishName;
            m_cfApi->searchMods(searchName, QString(), QString(), 0, 1, "popularity");
        }
    }
}

void ModLinkResolver::onCfSearchCompleted(const ModSearchResult &result)
{
    for (auto it = m_pendingLinks.begin(); it != m_pendingLinks.end(); ++it)
    {
        PendingLink &pending = it.value();
        if (!pending.cfDone)
        {
            pending.cfDone = true;
            if (!result.mods.isEmpty())
            {
                const ModInfo &first = result.mods.first();
                pending.info.curseforgeUrl = first.pageUrl.isEmpty()
                    ? QString("https://www.curseforge.com/minecraft/mc-mods/%1").arg(first.id)
                    : first.pageUrl;
                pending.info.id = first.id;
                if (!pending.info.curseforgeUrl.isEmpty())
                    pending.info.source = "curseforge";
            }
            emit linksResolved(pending.info);
            tryFinishMod(it.key());
            return;
        }
    }
}

void ModLinkResolver::onCfSearchFailed(const QString &error)
{
    Q_UNUSED(error);
    for (auto it = m_pendingLinks.begin(); it != m_pendingLinks.end(); ++it)
    {
        PendingLink &pending = it.value();
        if (!pending.cfDone)
        {
            pending.cfDone = true;
            emit linksResolved(pending.info);
            tryFinishMod(it.key());
            return;
        }
    }
}

// ============================================================
// Modrinth 哈希匹配回调
// ============================================================

void ModLinkResolver::onMrHashMatched(const QString &sha1, const QString &projectId, const ModVersionFile &version)
{
    Q_UNUSED(version);
    if (!m_mrSha1ToKey.contains(sha1))
        return;

    QString key = m_mrSha1ToKey.take(sha1);
    if (!m_pendingLinks.contains(key))
        return;

    PendingLink &pending = m_pendingLinks[key];

    if (!projectId.isEmpty()) {
        // 先设置 id/source 并立刻通知 UI（即使后续 fetchModDetail 失败也不阻塞）
        pending.info.id = projectId;
        pending.info.source = "modrinth";
        emit linksResolved(pending.info);

        m_mrProjectIdToKey.insert(projectId, key);
        m_mrApi->fetchModDetail(projectId);
    } else {
        // projectId 为空，回退到名称搜索
        QString searchName = pending.info.englishName.isEmpty()
            ? pending.info.name
            : pending.info.englishName;
        m_mrApi->searchMods(searchName, QString(), QString(), 0, 1, "relevance");
    }
}

void ModLinkResolver::onMrHashMatchFailed(const QString &sha1, const QString &error)
{
    Q_UNUSED(error);
    // 清理对应的 SHA-1 映射
    if (!sha1.isEmpty())
        m_mrSha1ToKey.remove(sha1);

    // 回退到名称搜索
    for (auto it = m_pendingLinks.begin(); it != m_pendingLinks.end(); ++it)
    {
        PendingLink &pending = it.value();
        if (pending.mrHashTried && !pending.mrDone)
        {
            QString searchName = pending.info.englishName.isEmpty()
                ? pending.info.name
                : pending.info.englishName;
            m_mrApi->searchMods(searchName, QString(), QString(), 0, 1, "relevance");
            return;
        }
    }
}

void ModLinkResolver::onMrDetailReceived(const ModInfo &detail)
{
    QString projectId = detail.id;
    if (!m_mrProjectIdToKey.contains(projectId))
        return;

    QString key = m_mrProjectIdToKey.take(projectId);
    if (!m_pendingLinks.contains(key))
        return;

    PendingLink &pending = m_pendingLinks[key];
    pending.info.modrinthUrl = detail.pageUrl;
    pending.info.id = detail.id;
    if (!pending.info.modrinthUrl.isEmpty())
        pending.info.source = "modrinth";
    pending.mrDone = true;

    emit linksResolved(pending.info);
    tryFinishMod(key);
}

void ModLinkResolver::onMrDetailFailed(const QString &error)
{
    Q_UNUSED(error);
    // fetchModDetail 失败：标记 mrDone 避免永久阻塞，此时 pending.info 已有 id/source
    for (auto it = m_pendingLinks.begin(); it != m_pendingLinks.end(); ++it)
    {
        PendingLink &pending = it.value();
        if (!pending.mrDone)
        {
            pending.mrDone = true;
            tryFinishMod(it.key());
            return;
        }
    }
}

void ModLinkResolver::onMrSearchCompleted(const ModSearchResult &result)
{
    for (auto it = m_pendingLinks.begin(); it != m_pendingLinks.end(); ++it)
    {
        PendingLink &pending = it.value();
        if (!pending.mrDone)
        {
            pending.mrDone = true;
            if (!result.mods.isEmpty())
            {
                const ModInfo &first = result.mods.first();
                pending.info.modrinthUrl = first.pageUrl;
                pending.info.id = first.id;
                if (!pending.info.modrinthUrl.isEmpty())
                    pending.info.source = "modrinth";
            }
            emit linksResolved(pending.info);
            tryFinishMod(it.key());
            return;
        }
    }
}

void ModLinkResolver::onMrSearchFailed(const QString &error)
{
    Q_UNUSED(error);
    for (auto it = m_pendingLinks.begin(); it != m_pendingLinks.end(); ++it)
    {
        PendingLink &pending = it.value();
        if (!pending.mrDone)
        {
            pending.mrDone = true;
            emit linksResolved(pending.info);
            tryFinishMod(it.key());
            return;
        }
    }
}

// ============================================================
// MC百科 回调
// ============================================================

void ModLinkResolver::onMcmodNameResolved(const QString &modId, const QString &chineseName,
                                           const QString &mcmodUrl)
{
    Q_UNUSED(chineseName);
    QString key = modId;
    if (!m_pendingLinks.contains(key))
    {
        for (auto it = m_pendingLinks.begin(); it != m_pendingLinks.end(); ++it)
        {
            if (it->info.id == modId)
            {
                key = it.key();
                break;
            }
        }
        if (!m_pendingLinks.contains(key))
            return;
    }

    PendingLink &pending = m_pendingLinks[key];
    pending.mcDone = true;
    if (!mcmodUrl.isEmpty())
        pending.info.mcmodUrl = mcmodUrl;
    else if (!chineseName.isEmpty())
        pending.info.mcmodUrl = QString("https://www.mcmod.cn/s?key=%1")
            .arg(QString(QUrl::toPercentEncoding(chineseName)));

    emit linksResolved(pending.info);
    tryFinishMod(key);
}

void ModLinkResolver::onMcmodLookupFailed(const QString &modId, const QString &englishName)
{
    Q_UNUSED(englishName);
    QString key = modId;
    if (!m_pendingLinks.contains(key))
    {
        for (auto it = m_pendingLinks.begin(); it != m_pendingLinks.end(); ++it)
        {
            if (it->info.id == modId)
            {
                key = it.key();
                break;
            }
        }
        if (!m_pendingLinks.contains(key))
            return;
    }

    PendingLink &pending = m_pendingLinks[key];
    pending.mcDone = true;

    emit linksResolved(pending.info);
    tryFinishMod(key);
}

// ============================================================
// 结果发射与队列处理
// ============================================================

void ModLinkResolver::tryFinishMod(const QString &key)
{
    if (!m_pendingLinks.contains(key))
        return;

    PendingLink &pending = m_pendingLinks[key];
    if (pending.cfDone && pending.mrDone && pending.mcDone)
    {
        m_pendingLinks.take(key);
        m_inFlight--;
        emit allDone();

        processQueue();
    }
}

// 保留旧方法签名避免链接错误，实际不再使用
void ModLinkResolver::tryEmitResult(const QString & /*key*/)
{
}
