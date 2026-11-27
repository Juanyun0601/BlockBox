/**
 * @file   MigrationService.h
 * @brief  迁移向导的后端：把旧启动器的账户 / 游戏目录 / Java / 登录服 / 设置搬进方块盒子
 * @author BlockBox Team
 * @date   2026-11-27
 *
 * 所有写操作都以「不碰原启动器」为前提：
 *   - 登记模式：只把原游戏目录加进实例文件夹列表；
 *   - 复制模式：把游戏目录整份复制到方块盒子自己的位置，再登记副本。
 * 复制由 GameDirCopyTask 在后台线程完成，界面靠 progress 信号显示进度。
 */
#ifndef MIGRATIONSERVICE_H
#define MIGRATIONSERVICE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QThread>

#include <atomic>

/** 用户在一次迁移里勾选的内容 */
struct MigrationSelection {
    QStringList gameDirs;      ///< 选中的游戏目录（.minecraft）
    bool accounts = false;     ///< 导入账户
    bool java = false;         ///< 导入 Java
    bool authServers = false;  ///< 导入第三方登录服务器
    bool downloadSettings = false; ///< 导入下载源 / 下载线程
    bool memory = false;       ///< 导入内存分配
    bool copyGameDirs = false; ///< true=复制一份，false=只登记
    QString copyRoot;          ///< 复制目标根目录（copyGameDirs 为 true 时使用）
};

/** 一次迁移的执行结果（供结果页汇总） */
struct MigrationApplyResult {
    bool ok = false;
    int addedAccounts = 0;
    int addedFolders = 0;
    int addedJavas = 0;
    int addedAuthServers = 0;
    int versionCount = 0;
    int copiedDirs = 0;
    bool downloadApplied = false;
    bool memoryApplied = false;
    QStringList notes;
    QStringList errors;
    QStringList importedDirs; ///< 最终登记进方块盒子的目录（复制模式下是副本）
};

/**
 * @brief 迁移向导后端
 *
 * 只负责数据写入与文件操作，不依赖任何界面控件，便于单测 / 探针直接调用。
 */
class MigrationService
{
public:
    /** 启动器 key 的显示名（与 LauncherImporter 保持一致） */
    static QString displayNameFor(const QString &key);

    /** 登记实例文件夹时用的显示名：「PCL 导入 · Minecraft」 */
    static QString folderNameFor(const QString &key, const QString &gameDir);

    /** 复制模式的默认根目录（方块盒子默认实例目录下的 Migrated） */
    static QString defaultCopyRoot();

    /** 在 root 下为 name 找一个不冲突的目录；已存在时追加 (2)/(3)… */
    static QString uniqueCopyDestination(const QString &root, const QString &name);

    /** 游戏目录里识别到的版本数 */
    static int countVersions(const QString &gameDir);

    /** 递归统计目录占用（字节；读不到的文件按 0 计） */
    static qint64 directorySize(const QString &gameDir);

    /** 目录是否可写（会尝试创建并删除一个临时文件） */
    static bool isWritableDir(const QString &dir);

    /** 复制目录前检查：目标不在源目录内部、磁盘空间够不够 */
    static bool canCopyTo(const QString &sourceDir, const QString &destination,
                          QString *reason);

    /**
     * @brief 导入账户 / Java / 登录服 / 下载设置 / 内存（不处理游戏目录）
     *
     * 重复内容自动跳过；结果里的 added* 是本次真正新增的数量。
     */
    static void applySettingsAndAccounts(const QString &key,
                                         const MigrationSelection &selection,
                                         MigrationApplyResult &result);

    /**
     * @brief 把一个游戏目录登记成实例文件夹
     * @param key            启动器 key（用于显示名；custom 表示手动添加的目录）
     * @param dir            要登记进方块盒子的目录
     * @param nameSourceDir  显示名参考目录（复制模式下传原目录，缺省同 dir）
     * @param result         累加结果
     * @return 是否新登记（已存在返回 false）
     */
    static bool registerGameDir(const QString &key, const QString &dir,
                                const QString &nameSourceDir,
                                MigrationApplyResult &result);

    /** 迁移后的轻量体检：目录里有没有版本、Java 文件还在不在 */
    static QStringList verify(const MigrationApplyResult &result,
                             const QStringList &javaPaths);
};

/** 后台复制一个游戏目录（保留相对目录结构，跳过符号链接） */
class GameDirCopyTask : public QThread
{
    Q_OBJECT

public:
    GameDirCopyTask(const QString &sourceDir, const QString &destinationDir,
                    QObject *parent = nullptr);
    ~GameDirCopyTask() override;

    /** 请求取消；线程会在当前文件复制完后停下来并清理未完成的副本 */
    void cancel();

signals:
    /** copied / total 为字节数；currentFile 是相对路径（用于界面提示） */
    void progress(qint64 copied, qint64 total, const QString &currentFile);
    /** 结束：error 为空表示成功，destination 是完整副本路径 */
    void finishedCopy(const QString &destination, const QString &error);

protected:
    void run() override;

private:
    QString m_sourceDir;
    QString m_destinationDir;
    std::atomic_bool m_cancel;
};

#endif // MIGRATIONSERVICE_H
