/**
 * @file   LauncherImporter.h
 * @brief  从主流 Minecraft 启动器（PCL2 / HMCL / BakaXL / 官方）导入数据
 * @author BlockBox Team
 *
 * 参考开源项目数据格式：
 *   - HMCL（huanghongxun/HMCL，GPL-3.0）：
 *     - 全局账户：%APPDATA%\hmcl\accounts.json
 *     - 账户字段：type("offline"/"microsoft"/"authlibInjector")、username、uuid、skin
 *   - PCL2 / 官方启动器：
 *     - .minecraft\launcher_profiles.json（authenticationDatabase）
 *     - .minecraft\launcher_accounts.json（新版官方，accounts map）
 *   - BakaXL：%APPDATA%\BakaXL\Profiles 为设备端加密存储，无法直接解析账户，
 *     仅支持检测存在 + 导入 .minecraft 版本目录。
 */
#ifndef LAUNCHERIMPORTER_H
#define LAUNCHERIMPORTER_H

#include <QList>
#include <QPair>
#include <QString>

#include "utils/SettingsManager.h"

/** 单个启动器的检测结果 */
struct LauncherDetect {
    QString key;            ///< "pcl" / "hmcl" / "multimc" / "baka" / "official"
    QString displayName;    ///< 显示名
    bool detected = false;  ///< 数据目录是否存在
    QString dataDir;        ///< 数据目录（%APPDATA% 下）
    QString minecraftDir;   ///< 关联的 .minecraft 目录（可能为空）
    int accountCount = 0;   ///< 检测到的可导入账户数（-1 表示不可读/未知）
    int versionCount = 0;   ///< 关联版本数
    QStringList versionNames; ///< 版本名列表（尽量短，用于提示）
    QString javaPath;       ///< 启动器配置的 Java 路径（可能为空）
    int javaMaxMemoryMb = -1; ///< 启动器配置的最大内存（MB，-1 未配置）
    int javaMinMemoryMb = -1; ///< 启动器配置的最小内存（MB，-1 未配置）
    QString detail;         ///< 状态详情文案
};

/** 一次导入的结果 */
struct LauncherImportResult {
    bool ok = false;
    int accountCount = 0;      ///< 成功导入账户数
    int versionCount = 0;      ///< 关联版本数
    QStringList versionNames;  ///< 版本名列表
    QString minecraftDir;      ///< 关联的 .minecraft 目录
    QString javaPath;          ///< 启动器配置的 Java 路径（可能为空）
    int javaMaxMemoryMb = -1;  ///< 启动器配置的最大内存（MB，-1 未配置）
    int javaMinMemoryMb = -1;  ///< 启动器配置的最小内存（MB，-1 未配置）
    QStringList instanceDirs;  ///< 额外注册的实例目录（MultiMC 每个 instance 一个 .minecraft）
    QString detail;            ///< 结果详情
};

class LauncherImporter
{
public:
    /** 检测全部受支持启动器 */
    static QList<LauncherDetect> detectAll();

    /** 检测单个启动器（供"重新检测"按钮使用） */
    static LauncherDetect detectOne(const QString &key);

    /** 从指定启动器导入（账户 + 版本目录） */
    static LauncherImportResult importFrom(const QString &key);

    /** 解析指定启动器的账户列表（不写入任何设置，供调用方使用） */
    static QList<AccountInfo> importAccounts(const QString &key);

    /** %APPDATA% 路径（Windows Roaming） */
    static QString appDataDir();

    /** 定位启动器关联的 .minecraft 目录（存在 versions 子目录才返回） */
    static QString findMinecraftDir(const QString &key);

private:
    /** 解析 HMCL accounts.json（多种结构宽容解析） */
    static QList<AccountInfo> parseHmclAccounts(const QString &path);

    /** 解析官方/PCL2 的 launcher_profiles.json / launcher_accounts.json */
    static QList<AccountInfo> parseOfficialAccounts(const QString &path);

    /** 解析 MultiMC / Prism Launcher 的 accounts.json（type: MSA/Offline/Yggdrasil） */
    static QList<AccountInfo> parseMultiMCAccounts(const QString &path);

    /** 收集 .minecraft/versions 下的版本名（子目录名） */
    static QStringList versionsIn(const QString &minecraftDir);

    /** MultiMC/Prism：遍历 instances 目录下 mmc-pack.json 收集版本名 */
    static QStringList multiMcInstanceVersions(const QString &dataDir);

    /** MultiMC/Prism：读取 prism.cfg / mmc.cfg / instance.cfg 的 JavaPath */
    static QString multiMcJavaPath(const QString &dataDir);

    /** HMCL：从 hmcl.json 读取 Java 路径（java / java8 字段） */
    static QString hmclJavaPath(const QString &dataDir);

    /** HMCL：从 hmcl.json 读取内存配置，返回 (maxMB, minMB)，-1 表示未配置 */
    static QPair<int, int> hmclJavaMemory(const QString &dataDir);

    /** MultiMC/Prism：读取 cfg 的内存配置（MaxMemAlloc/MinMemAlloc），返回 (maxMB, minMB) */
    static QPair<int, int> multiMcJavaMemory(const QString &dataDir);

    /** 统计 .minecraft/versions 下版本目录数 */
    static int countVersions(const QString &minecraftDir);
};

#endif // LAUNCHERIMPORTER_H
