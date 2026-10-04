/**
 * @file   WorldBackupManager.h
 * @brief  世界备份与恢复管理器声明
 * @author BlockBox Team
 * @date   2026-08-29
 *
 * 功能（对应「方块盒子待实现功能提案」1.1 世界备份与恢复）：
 *   - 将世界存档压缩备份到实例目录之外（Platform::getBackupDirectory()），
 *     防止误删实例目录导致备份丢失；
 *   - 备份列表查询、手动/自动备份、一键恢复、删除旧备份与保留策略；
 *   - 启动游戏前按配置频率（每次启动 / 每小时 / 每天）自动备份有改动的世界。
 *
 * 备份布局：
 *   <Data>/Backups/Worlds/<实例名>_<路径哈希>/<世界名>/<时间戳>_<随机>.zip
 *   同名 .json 侧车文件保存备份元数据（时间、原始大小、压缩大小、备注等）。
 *
 * 同步核心函数（createBackupSync/restoreBackupSync/deleteBackupSync）为耗时
 * 操作，设计为可在工作线程调用；UI 通过 createBackupAsync/restoreBackupAsync
 * 封装触发，结果经 backupFinished/restoreFinished 信号返回。
 */
#ifndef WORLDBACKUPMANAGER_H
#define WORLDBACKUPMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <functional>

/** 单个世界备份的元数据 */
struct WorldBackupInfo
{
    QString id;             ///< 备份ID（时间戳 + 随机后缀，同时是文件名主干）
    QString worldName;      ///< 世界目录名
    QDateTime createdAt;    ///< 备份创建时间
    qint64 sourceSize = 0;  ///< 备份时世界原始大小（字节）
    qint64 archiveSize = 0; ///< 压缩包大小（字节）
    bool automatic = false; ///< 是否为自动备份
    QString note;           ///< 备注
    QString instanceName;   ///< 所属实例名
    QString gameVersion;    ///< 备份时的 MC 版本（可能为空）
    QString archivePath;    ///< 压缩包绝对路径（运行时填充，不写入元数据）

    /** 压缩包是否仍存在于磁盘（元数据可能因手动删除成为孤儿） */
    bool exists() const;
};

class WorldBackupManager : public QObject
{
    Q_OBJECT

public:
    static WorldBackupManager *instance();

    // ── 路径计算 ──

    /// 备份根目录（实例目录之外）
    static QString backupsRoot();
    /// 指定实例的备份目录
    static QString instanceBackupDir(const QString &instancePath);
    /// 指定实例内指定世界的备份目录
    static QString worldBackupDir(const QString &instancePath, const QString &worldName);

    /// 实例的存档目录（版本隔离感知，与游戏实际读取的 saves 目录一致）
    static QString savesDirForInstance(const QString &instancePath);
    /// 列出实例存档目录下的世界名（目录名列表，按名称排序）
    static QStringList listWorldNames(const QString &instancePath);
    /// 递归计算目录大小（字节）
    static qint64 directorySize(const QString &dirPath);

    // ── 备份列表 ──

    /// 列出某世界的全部备份（按创建时间从新到旧），跳过压缩包已丢失的孤儿元数据
    QList<WorldBackupInfo> listBackups(const QString &instancePath,
                                       const QString &worldName) const;

    // ── 同步核心（耗时，调用方应放入工作线程） ──

    /**
     * @brief 将世界目录压缩备份
     * @param note        备注（可为空）
     * @param automatic   是否为自动备份（仅影响显示标记）
     * @param outInfo     输出新备份元数据，可为 nullptr
     * @param error       输出错误信息，可为 nullptr
     * @return 成功返回 true
     */
    bool createBackupSync(const QString &instancePath, const QString &worldName,
                          const QString &note, bool automatic,
                          WorldBackupInfo *outInfo = nullptr, QString *error = nullptr);

    /**
     * @brief 将世界恢复到指定备份点
     *
     * 恢复前若设置允许（getWorldBackupSnapshotBeforeRestore），会先把当前世界
     * 备份为一份自动快照，保证恢复操作本身可回退；随后删除当前世界目录并解压
     * 备份包到原位置。
     */
    bool restoreBackupSync(const QString &instancePath, const QString &worldName,
                           const QString &backupId, QString *error = nullptr);

    /// 删除指定备份（压缩包与元数据）
    bool deleteBackupSync(const QString &instancePath, const QString &worldName,
                          const QString &backupId, QString *error = nullptr);

    // ── 异步封装（UI 线程调用，结果经信号返回） ──

    void createBackupAsync(const QString &instancePath, const QString &worldName,
                           const QString &note, bool automatic);
    void restoreBackupAsync(const QString &instancePath, const QString &worldName,
                            const QString &backupId);

    // ── 保留策略 ──

    /**
     * @brief 按保留策略删除旧备份
     * @param keepCount 从新到旧保留的份数
     * @return 实际删除的备份数
     */
    int applyRetentionPolicy(const QString &instancePath, const QString &worldName,
                             int keepCount);

    // ── 启动前自动备份 ──

    /**
     * @brief 启动游戏前按配置执行自动备份（每次启动 / 每小时 / 每天）
     *
     * 备份自上次备份后有改动的世界。因游戏启动后会改写存档，本函数在内部
     * 事件循环中等待备份完成后再返回（UI 不冻结，进度经 progress 回调上报）。
     * @return 是否执行了备份（无改动/被禁用/频率未到时返回 false）
     */
    bool autoBackupBeforeLaunch(const QString &instancePath,
                                const std::function<void(int, const QString &)> &progress = {});

signals:
    /// 备份/恢复过程中的粗粒度进度（percent: 0-100，-1 表示不定进度）
    void backupProgress(int percent, const QString &message);
    void backupFinished(const QString &instancePath, const QString &worldName,
                        bool ok, const QString &message, bool automatic);
    void restoreFinished(const QString &instancePath, const QString &worldName,
                         bool ok, const QString &message);
    /// 启动前自动备份批次完成（backed: 成功份数，failed: 失败份数）
    void autoBackupBatchFinished(int backed, int failed);

private:
    explicit WorldBackupManager(QObject *parent = nullptr);

    /// 启动前自动备份批次的执行体（在工作线程中调用，逐个世界备份）
    void runAutoBackupBatch(const QString &instancePath, const QStringList &worlds);

    /// 从实例版本 json 读取游戏版本号（失败返回空串）
    static QString readGameVersion(const QString &instancePath);
    /// 世界目录的"最后修改时间"（level.dat 修改时间与目录时间取较新者）
    static QDateTime worldModifiedTime(const QString &worldPath);
    /// 最近一次自动备份完成时间
    QDateTime lastAutoBackupTime() const;
    void setLastAutoBackupTime(const QDateTime &time);

    bool m_busy = false; ///< 防止备份/恢复并发执行
};

#endif // WORLDBACKUPMANAGER_H
