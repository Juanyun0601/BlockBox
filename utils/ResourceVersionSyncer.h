#ifndef RESOURCEVERSIONSYNCER_H
#define RESOURCEVERSIONSYNCER_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "utils/mod/ModData.h"
#include "utils/content/ContentData.h"

class ModrinthAPI;
class ContentDownloader;

/**
 * @brief 参与版本同步转化的单条资源信息
 *
 * 修改实例版本后，模组 / 资源包 / 光影包会被同步到支持新游戏版本的版本；
 * 本结构既描述转化成功的资源，也描述未能适配新版本、需要用户处理的资源。
 */
struct ResourceSyncItem
{
    QString filePath;    ///< 本地文件路径（禁用状态的模组带 .disabled 后缀）
    QString fileName;    ///< 磁盘上的文件名
    QString displayName; ///< 展示名（去掉 .disabled 后缀）
    ContentType type = ContentType::Mod; ///< 资源类型（模组 / 资源包 / 光影包）
    bool enabled = true; ///< 模组是否处于启用状态（资源包 / 光影包恒为 true）
    QString sha1;        ///< 文件 SHA-1（小写）
    bool identified = false;    ///< 是否成功匹配到 Modrinth 项目
    QString installedVersion;   ///< 当前安装的版本名（识别成功时）
    QStringList gameVersions;   ///< 当前文件支持的 MC 版本列表
    QString note;        ///< 待处理原因（空 = 已成功转化）
};

/**
 * @brief 实例资源版本同步器
 *
 * 修改实例版本后自动把 mods / resourcepacks / shaderpacks 里的资源
 * 转化到支持新游戏版本的版本：
 *   1. 收集三类资源文件并后台计算 SHA-1；
 *   2. Modrinth 批量指纹匹配识别资源及其当前版本；
 *   3. 已支持新版本的跳过；其余查询支持新版本（模组同时按加载器过滤）的目标版本；
 *   4. 有目标版本 → 镜像优先下载并替换旧文件（保留禁用状态）；
 *      没有 / 识别不出 / 下载失败 → 汇总进待处理清单交由用户决定。
 */
class ResourceVersionSyncer : public QObject
{
    Q_OBJECT

public:
    explicit ResourceVersionSyncer(QObject *parent = nullptr);
    ~ResourceVersionSyncer() override;

    /** 是否正在同步中（防止并发启动） */
    bool isRunning() const { return m_running; }

    /**
     * @brief 启动一次版本同步转化
     * @param instancePath 实例目录
     * @param oldVersion   修改前的游戏版本（可为空）
     * @param newVersion   修改后的游戏版本
     */
    void start(const QString &instancePath, const QString &oldVersion, const QString &newVersion);

signals:
    /** 同步全部结束：converted 为已转化的资源，pending 为需要用户处理的资源 */
    void finished(const QList<ResourceSyncItem> &converted, const QList<ResourceSyncItem> &pending);
    /** 指纹匹配 / 版本查询失败（未对实例做任何修改） */
    void checkFailed(const QString &error);

private:
    struct DownloadJob
    {
        ResourceSyncItem item; ///< 原资源（旧文件路径、禁用状态）
        ModVersionFile target; ///< 支持新版本的目标版本文件
    };

    void startMatch();
    void onHashesMatched(const QMap<QString, ModVersionFile> &versions);
    void startUpdateChecks();
    void onUpdatesChecked(const QMap<QString, ModVersionFile> &versions, bool forMods);
    void startNextDownload();
    void downloadJobFile(const QString &url, const QString &fileName, bool allowFallback);
    static void finalizeReplacedItem(ResourceSyncItem &item, const QString &destPath);
    void finish();

    QList<ResourceSyncItem> m_items;      ///< 本轮收集到的全部资源
    QList<ResourceSyncItem> m_converted;  ///< 已转化完成的资源
    QList<ResourceSyncItem> m_pending;    ///< 需要用户处理的资源
    QList<DownloadJob> m_downloadQueue;   ///< 待下载替换队列
    QList<ResourceSyncItem> m_modCheckItems;    ///< 待查询目标版本的模组
    QList<ResourceSyncItem> m_otherCheckItems;  ///< 待查询目标版本的资源包 / 光影包
    DownloadJob m_activeJob;              ///< 当前正在下载的资源

    QString m_instancePath;
    QString m_newVersion;
    QString m_loader;                     ///< 实例加载器（模组查询过滤用）
    bool m_running = false;

    QString m_taskId;                     ///< 任务栏显示的同步任务
    int m_downloadDone = 0;               ///< 已处理完的下载数

    ModrinthAPI *m_matchApi = nullptr;    ///< 指纹匹配
    ModrinthAPI *m_modsApi = nullptr;     ///< 模组目标版本查询（带加载器过滤）
    ModrinthAPI *m_othersApi = nullptr;   ///< 资源包 / 光影包目标版本查询
    ContentDownloader *m_activeDownloader = nullptr; ///< 当前进行中的下载

    bool m_modsCheckDone = false;
    bool m_othersCheckDone = false;
};

#endif // RESOURCEVERSIONSYNCER_H
