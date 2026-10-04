/**
 * @file   ModNameFetcher.h
 * @brief  模组中文名/英文名获取协调器
 * @author BlockBox Team
 * @date   2026-06-19
 *
 * 参考开源项目（HMCL、PCL、PrismLauncher）的做法，整合多个数据源：
 * - MCModAPI（MC百科）：查询模组中文名
 * - ModrinthAPI：查询模组英文官方名
 * - 内置离线数据库 mcmod_names.json 作为优先缓存
 */
#ifndef MODNAMEFETCHER_H
#define MODNAMEFETCHER_H

#include <QObject>
#include <QMap>
#include <QQueue>
#include <QSet>
#include "ModData.h"

class MCModAPI;
class ModrinthAPI;

class ModNameFetcher : public QObject
{
    Q_OBJECT

public:
    explicit ModNameFetcher(QObject *parent = nullptr);
    ~ModNameFetcher();

    /** 为单个模组异步获取中文名和英文名 */
    void fetchNames(const ModInfo &modInfo);

    /**
     * @brief 批量获取模组名称
     * @param modList 模组列表
     * @param maxConcurrent 最大并发数（默认 3，避免 API 限流）
     */
    void fetchNamesBatch(const QList<ModInfo> &modList, int maxConcurrent = 3);

    /** 清除缓存 */
    void clearCache();

    /** 是否有正在进行的请求 */
    bool isBusy() const;

    /** 获取 MCModAPI 实例指针（供外部直接使用） */
    MCModAPI *mcmodApi() const { return m_mcmodApi; }

    /** 获取 ModrinthAPI 实例指针（供外部直接使用） */
    ModrinthAPI *modrinthApi() const { return m_modrinthApi; }

signals:
    /** 单个模组名称解析完成（可能部分失败） */
    void namesResolved(const ModInfo &modInfo);

    /** 单个模组名称解析完全失败 */
    void namesFetchFailed(const QString &modId, const QString &englishName);

    /** 批量解析进度更新 */
    void batchProgress(int resolved, int total);

    /** 批量解析全部完成 */
    void batchCompleted(int resolved, int failed);

private slots:
    void onMcmodNameResolved(const QString &modId, const QString &chineseName, const QString &mcmodUrl);
    void onMcmodLookupFailed(const QString &modId, const QString &englishName);
    void onModrinthDetailReceived(const ModInfo &detail);
    void onModrinthDetailFailed(const QString &error);

private:
    struct PendingMod
    {
        ModInfo info;
        bool modrinthDone = false;
        bool mcmodDone = false;
    };

    void processQueue();
    void resolveMod(PendingMod &pending);
    void tryEmitResult(const QString &key);

    /** 从本地 JSON 文件加载持久化缓存 */
    void loadPersistentCache();
    /** 将内存缓存保存到本地 JSON 文件 */
    void savePersistentCache();
    /** 获取缓存文件路径 */
    QString cacheFilePath() const;

    MCModAPI *m_mcmodApi;
    ModrinthAPI *m_modrinthApi;

    // 队列管理
    QQueue<ModInfo> m_queue;
    QMap<QString, PendingMod> m_pendingMods; // key = modId or fileName
    int m_inFlight = 0;
    int m_maxConcurrent = 3;
    int m_resolvedCount = 0;
    int m_failedCount = 0;
    int m_totalCount = 0;

    // 缓存：modId -> (chineseName, englishName)
    struct NameCache
    {
        QString chineseName;
        QString englishName;
        QString mcmodUrl;
    };
    QMap<QString, NameCache> m_cache;

    // 去重：已提交请求的 key，避免重复
    QSet<QString> m_submitted;
};

#endif // MODNAMEFETCHER_H