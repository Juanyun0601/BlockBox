#ifndef MODLINKRESOLVER_H
#define MODLINKRESOLVER_H

#include <QObject>
#include <QMap>
#include <QQueue>
#include "ModData.h"

class CurseForgeAPI;
class ModrinthAPI;
class MCModAPI;

class ModLinkResolver : public QObject
{
    Q_OBJECT

public:
    explicit ModLinkResolver(QObject *parent = nullptr);
    ~ModLinkResolver();

    void resolveLinks(const ModInfo &modInfo);
    bool isBusy() const;

signals:
    /** 任一平台解析完成时立即发出，UI 可增量更新 */
    void linksResolved(const ModInfo &modInfo);
    /** 所有平台（CF/MR/MC）均解析完成，整体解析工作结束 */
    void allDone();

private slots:
    // CurseForge
    void onCfFingerprintCompleted(quint32 fingerprint, const QString &modId, const QString &pageUrl);
    void onCfFingerprintFailed(const QString &error);
    void onCfSearchCompleted(const ModSearchResult &result);
    void onCfSearchFailed(const QString &error);

    // Modrinth
    void onMrHashMatched(const QString &sha1, const QString &projectId, const ModVersionFile &version);
    void onMrHashMatchFailed(const QString &sha1, const QString &error);
    void onMrDetailReceived(const ModInfo &detail);
    void onMrDetailFailed(const QString &error);
    void onMrSearchCompleted(const ModSearchResult &result);
    void onMrSearchFailed(const QString &error);

    // MC百科
    void onMcmodNameResolved(const QString &modId, const QString &chineseName, const QString &mcmodUrl);
    void onMcmodLookupFailed(const QString &modId, const QString &englishName);

private:
    void processQueue();
    void tryEmitResult(const QString &key);
    /** 检查指定 key 是否全部平台完成，若是则 emit allDone 并 processQueue */
    void tryFinishMod(const QString &key);

    CurseForgeAPI *m_cfApi;
    ModrinthAPI *m_mrApi;
    MCModAPI *m_mcApi;

    struct PendingLink
    {
        ModInfo info;
        bool cfDone = false;
        bool mrDone = false;
        bool mcDone = false;
        // 是否已尝试哈希匹配（哈希匹配失败后回退到名称搜索）
        bool cfHashTried = false;
        bool mrHashTried = false;
    };

    QQueue<ModInfo> m_queue;
    QMap<QString, PendingLink> m_pendingLinks;

    // 哈希值 → pending key 的映射，用于回调时查找
    QMap<quint32, QString> m_cfFingerprintToKey;
    QMap<QString, QString> m_mrSha1ToKey;
    // Modrinth project detail 回调查找
    QMap<QString, QString> m_mrProjectIdToKey;

    int m_inFlight = 0;
    static constexpr int kMaxConcurrent = 1;
};

#endif // MODLINKRESOLVER_H
