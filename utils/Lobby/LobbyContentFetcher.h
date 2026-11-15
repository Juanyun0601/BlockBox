/**
 * @file   LobbyContentFetcher.h
 * @brief  联机大厅「一键加入」用：把房间要求但本机没有的模组 / 资源包 / 光影包按文件名找回来下载
 * @author BlockBox Team
 * @date   2026-11-14
 *
 * 房间里的内容只有文件名（房主实例里的那个 jar / zip），本地没有时按下面的顺序找：
 *   1. Modrinth 按文件名里的模组名搜索 → 取项目版本列表；
 *   2. 版本文件里优先挑「文件名完全相同」的那个，其次挑版本 / 加载器兼容的最新文件；
 *   3. Modrinth 没找到再走 CurseForge 同样的流程。
 * 找到就按实例对应的目录（mods / resourcepacks / shaderpacks）下载，失败会带一句能看懂的提示。
 *
 * 串行处理：一次只处理一个文件，界面能显示「正在准备 3/7」，也避免把下载源问限流。
 */

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "utils/content/ContentData.h"
#include "utils/mod/ModData.h"

class ContentDownloader;
class CurseForgeAPI;
class ModrinthAPI;
class QTimer;

/** 要找回的一个文件（房间里的文件名 + 内容类型） */
struct LobbyFetchRequest
{
    QString fileName;                          ///< 房主实例里的文件名
    ContentType type = ContentType::Mod;       ///< 模组 / 资源包 / 光影包
    bool optional = false;                     ///< 是否可选项（辅助模组、资源包、光影包）
};

/** 单个文件的下载结果 */
struct LobbyFetchResult
{
    LobbyFetchRequest request;
    bool ok = false;          ///< 是否下载成功
    bool attempted = false;   ///< 是否真的找过（取消时未处理的为 false）
    QString savedName;        ///< 实际写入实例的文件名
    QString source;           ///< Modrinth / CurseForge
    QString message;          ///< 失败原因（面向普通用户）
};

class LobbyContentFetcher : public QObject
{
    Q_OBJECT

public:
    explicit LobbyContentFetcher(QObject *parent = nullptr);
    ~LobbyContentFetcher() override;

    /** 开始按顺序找回这些文件；已在运行时先取消上一次 */
    void start(const QString &instancePath, const QString &gameVersion, const QString &loader,
               const QList<LobbyFetchRequest> &requests);

    /** 取消：当前下载会被中断，未处理的算失败 */
    void cancel();

    bool isRunning() const { return m_running; }
    /** 已完成的结果（下标与 start() 传入的列表一致，未处理的位置为空结构） */
    QList<LobbyFetchResult> results() const { return m_results; }

signals:
    /** 每个文件处理完（成功或失败）时发一次 */
    void itemFinished(int index, const LobbyFetchResult &result);
    /** 进度：已完成几个 / 共几个 + 当前在做什么（直接显示在准备界面） */
    void progressChanged(int finishedCount, int total, const QString &message);
    /** 全部处理完：成功几个、失败几个 */
    void finished(int okCount, int failCount);

private:
    void processNext();
    void beginModrinth();
    void beginCurseForge();
    void requestDownload(const QString &url, const QString &fileName);
    void finishItem(bool ok, const QString &message, const QString &savedName,
                    const QString &source);
    void finishAll();
    /** 武装超时看门狗：接口请求默认 20 秒，下载阶段给更长的静默容忍（进度会重新计时） */
    void armWatchdog(int timeoutMs = 0);
    void stopWatchdog();

    // 各平台的回调
    void onMrSearchDone(const ModSearchResult &result);
    void onMrSearchFailed(const QString &error);
    void onMrDetailDone(const ModInfo &detail);
    void onMrDetailFailed(const QString &error);
    void onCfSearchDone(const ModSearchResult &result);
    void onCfSearchFailed(const QString &error);
    void onCfDetailDone(const ModInfo &detail);
    void onCfDetailFailed(const QString &error);

    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &error);
    /** 下载有进度就把看门狗往后推：大文件（资源包 / 光影包）不会被误判超时 */
    void downloadProgress(qint64 received, qint64 total);

    /** 当前文件在某个平台的候选列表里换下一个项目试 */
    bool tryNextMrCandidate();
    bool tryNextCfCandidate();
    /** 从搜索命中里挑最像的文件名对应的项目（按名称相似度排序） */
    QList<ModInfo> rankCandidates(const QList<ModInfo> &hits, const QString &query) const;
    /** 从项目的版本列表里挑要下的文件；返回 false 表示没有合适的 */
    bool pickVersionFile(const ModInfo &detail, ModVersionFile *fileOut) const;

    QString currentQuery() const;
    bool advanceQuery();

    ContentDownloader *m_downloader = nullptr;
    ModrinthAPI *m_mr = nullptr;
    CurseForgeAPI *m_cf = nullptr;
    QTimer *m_watchdog = nullptr;

    QString m_instancePath;
    QString m_gameVersion;
    QString m_loader;
    QList<LobbyFetchRequest> m_requests;
    QList<LobbyFetchResult> m_results;

    int m_index = -1;          ///< 当前处理的请求下标
    int m_okCount = 0;
    int m_failCount = 0;
    bool m_running = false;
    bool m_cancelled = false;
    bool m_busyDownload = false;

    /** 当前这一步在做什么：搜索 / 取详情 / 下载（防止超时后的旧回调串到下一项） */
    enum class Stage
    {
        Idle,
        Searching,
        Detailing,
        Downloading
    };
    Stage m_stage = Stage::Idle;
    QString m_expectedDetailId;   ///< 正在取详情的 Modrinth 项目 id
    QString m_expectedCfDetailId; ///< 正在取详情的 CurseForge 项目 id

    // 当前请求的搜索状态
    QStringList m_queries;      ///< 待尝试的搜索词（前缀词优先，整串兜底）
    int m_queryIndex = 0;
    QList<ModInfo> m_mrCandidates; ///< Modrinth 搜索命中，按相似度排好
    int m_mrCandidateIndex = 0;
    QList<ModInfo> m_cfCandidates; ///< CurseForge 搜索命中
    int m_cfCandidateIndex = 0;
    QString m_platform;            ///< 当前内容是从哪个源找到的
};
