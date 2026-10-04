/**
 * @file   BedrockInstanceManager.h
 * @brief  基岩版实例管理器 — 多实例数据隔离
 * @author BlockBox Team
 *
 * 基岩版（Windows UWP）的游戏数据固定在
 *   %LOCALAPPDATA%\Packages\<PackageFamilyName>\LocalState\games\com.mojang
 * 无法像 Java 版那样通过启动参数 / 独立目录切换实例。
 *
 * 本管理器采用 Windows 目录联接（junction）方案实现多实例：
 *   - 每个实例拥有独立数据目录 <数据目录>/bedrock/instances/<id>/com.mojang
 *   - 启动某个实例前，把系统 com.mojang 目录替换为指向该实例数据的 junction
 *   - 首次使用自动把已有游戏数据迁移进「默认数据」实例，避免数据丢失
 *
 * 参考 BedrockBoot 的「游戏文件夹 / 数据隔离」思路（见开源项目参考），
 * 结合 BlockBox 现有 BedrockLauncher（检测 + 启动 UWP 应用）实现。
 */

#pragma once

#include <QDateTime>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

/** 基岩版实例信息 */
struct BedrockInstance {
    QString id;              // 唯一 ID（用于目录名与持久化）
    QString name;            // 显示名称
    QString dataDir;         // 实例数据目录（junction 目标，内含 com.mojang 子目录）
    QString version;         // 最近一次检测到的游戏版本
    QDateTime createdAt;
    QDateTime lastPlayed;
};

class BedrockInstanceManager : public QObject
{
    Q_OBJECT

public:
    static BedrockInstanceManager *instance();

    /** 首次调用时迁移现有游戏数据并创建默认实例（幂等） */
    void ensureInitialized();

    QVector<BedrockInstance> instances() const;
    BedrockInstance instanceById(const QString &id) const;

    /** 新建实例；importDir 非空时将其内容复制为实例初始数据 */
    BedrockInstance createInstance(const QString &name, const QString &importDir = QString());
    bool renameInstance(const QString &id, const QString &newName);
    bool removeInstance(const QString &id);

    QString activeInstanceId() const;
    BedrockInstance activeInstance() const;

    /** 激活实例：建立 junction，让系统 com.mojang 指向该实例数据目录 */
    bool activateInstance(const QString &id, QString *errorMessage = nullptr);

    /** 系统基岩版游戏数据根目录（com.mojang），未安装时返回空 */
    QString gameDataRoot() const;

    /** 实例数据目录（com.mojang）真实路径 */
    QString instanceDataDir(const QString &id) const;

    /** 更新实例版本信息 */
    void refreshVersionInfo(const QString &id, const QString &version);

    bool isInitialized() const { return m_initialized; }

signals:
    void instancesChanged();

private:
    explicit BedrockInstanceManager(QObject *parent = nullptr);

    bool loadFromDisk();
    bool saveToDisk();
    QString instancesDir() const;
    QString generateId() const;
    void setActiveInstance(const QString &id);
    int indexOf(const QString &id) const;
    /** 把旧位置（BlockBox/bedrock/instances）的实例数据迁移到独立的基岩数据目录 */
    void migrateDataDirs();

    // junction 工具（Windows）
    static bool createJunction(const QString &linkPath, const QString &targetPath, QString *err);
    static bool isJunction(const QString &path);
    static QString junctionTarget(const QString &path);
    static bool removeJunction(const QString &path);
    static bool copyRecursively(const QString &srcDir, const QString &dstDir, QString *err);

    static BedrockInstanceManager *m_instance;
    static QMutex m_mutex;

    QVector<BedrockInstance> m_instances;
    QString m_activeInstanceId;
    bool m_initialized = false;
};
