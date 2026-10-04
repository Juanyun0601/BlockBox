/**
 * @file   LocalCompatibilityChecker.h
 * @brief  本地资源兼容性检测引擎声明
 * @author BlockBox Team
 * @date   2026-08-30
 *
 * 纯离线检测实例内的模组/资源包/光影包与当前游戏版本、加载器的兼容性，
 * 算法参考 PrismLauncher（pack_format 映射、元数据解析）与
 * HMCL（重复模组、依赖缺失、无法识别文件等 mods/ 目录检查）。
 */
#ifndef LOCALCOMPATIBILITYCHECKER_H
#define LOCALCOMPATIBILITYCHECKER_H

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

/**
 * @brief 单条兼容性问题
 */
struct CompatIssue
{
    enum Severity
    {
        Error = 0,     ///< 会导致启动失败或功能异常
        Warning = 1,   ///< 可能存在问题，建议处理
        Info = 2       ///< 提示信息
    };

    int severity = Error;
    QString category;     ///< "mod" / "resourcepack" / "shaderpack"
    QString fileName;     ///< 涉及的文件名（可多个，用逗号分隔）
    QString title;        ///< 问题标题
    QString detail;       ///< 详细说明
    QString suggestion;   ///< 处理建议
};

/**
 * @brief 兼容性检测报告
 */
struct CompatReport
{
    QString gameVersion;              ///< 实例的 Minecraft 版本（可能为空）
    QString loaderType;               ///< 实例加载器（Forge/Fabric/NeoForge/Quilt，可能为空）
    int modCount = 0;                 ///< 检查的模组数（含禁用）
    int resourcePackCount = 0;        ///< 检查的资源包数
    int shaderPackCount = 0;          ///< 检查的光影包数
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    QList<CompatIssue> issues;
};

/**
 * @brief 本地资源兼容性检测器
 *
 * 检测项：
 *  - 模组：加载器不匹配、重复模组 ID、重复文件、前置依赖缺失、
 *          依赖版本范围不满足、依赖冲突(breaks)、与游戏版本不符、
 *          OptiFine 与 Fabric 冲突、mods/ 目录无法识别的文件
 *  - 资源包：pack.mcmeta 缺失/损坏、pack_format 与游戏版本不匹配、缺少 pack.png
 *  - 光影包：缺少 shaders/ 目录（不是有效光影包）、未安装 Iris/OptiFine/Oculus
 */
class LocalCompatibilityChecker
{
public:
    /**
     * @brief 检测实例内所有本地资源
     * @param instancePath 实例（.minecraft 或 versions/{ver}）路径
     * @param gameVersion  实例的 MC 版本，未知传空字符串
     * @param loaderType   实例加载器（Forge/Fabric/NeoForge/Quilt），原版传空字符串
     */
    static CompatReport checkInstance(const QString &instancePath,
                                      const QString &gameVersion,
                                      const QString &loaderType);

    // ========== 供对话框/其他模块复用的版本工具 ==========

    /** 比较 MC/模组版本（如 "1.21.10" > "1.21.9"），未知段按 0 处理 */
    static int compareVersions(const QString &a, const QString &b);

    /**
     * @brief 判断 semver 版本是否满足范围表达式（node-semver 子集）
     * @param version 实际版本，如 "1.20.1"
     * @param range   范围表达式，如 ">=1.20 <1.22"、"1.16.x"、"*"、"~2.3.0"
     */
    static bool semverSatisfies(const QString &version, const QString &range);

    /**
     * @brief 判断版本是否满足 Forge/NeoForge TOML 版本范围
     * @param version 实际版本
     * @param range   如 "[1.20,)"、"(,1.21)"、"[47.0.47,48)"、"1.20"
     */
    static bool forgeRangeSatisfies(const QString &version, const QString &range);

    /** 游戏版本对应的资源包 pack_format（未知返回 -2；版本本身无法识别返回 -1） */
    static int resourcePackFormatFor(const QString &gameVersion);

private:
    LocalCompatibilityChecker() = default;

    // ===== 模组元数据 =====
    struct TomlDep
    {
        QString modId;
        QString range;
        bool required = true;
    };

    struct ModMeta
    {
        QString fileName;
        QString filePath;
        bool enabled = true;
        QString modId;          // 声明的 mod id（fabric/forge 皆取第一个）
        QString version;
        QString loader;         // "fabric" / "quilt" / "forge" / "neoforge" / 空
        QStringList provides;   // 额外提供的 mod id（Fabric API 子模块等）
        QMap<QString, QString> depends;  // modId → 版本范围（空串表示无范围）
        QMap<QString, QString> breaks;   // modId → 冲突版本范围
        QList<TomlDep> tomlDeps;         // Forge/NeoForge [[dependencies.x]]
        bool isOptifine = false;
    };

    static bool parseModJar(const QString &filePath, const QString &fileName, bool enabled, ModMeta &meta);
    static bool parseFabricModJson(const QString &content, ModMeta &meta);
    static bool parseQuiltModJson(const QString &content, ModMeta &meta);
    static bool parseModsToml(const QString &content, ModMeta &meta, const QString &loader);

    static void checkMods(CompatReport &report,
                          const QList<ModMeta> &mods,
                          const QStringList &junkFiles,
                          const QString &gameVersion,
                          const QString &loaderType);
    static void checkResourcePacks(CompatReport &report,
                                   const QString &resourcePackDir,
                                   const QString &gameVersion);
    static void checkShaderPacks(CompatReport &report,
                                 const QString &shaderPackDir,
                                 const QList<ModMeta> &mods,
                                 const QString &loaderType);

    static void addIssue(CompatReport &report, const CompatIssue &issue);
    /** 收集目录下指定后缀的文件（不含子目录），返回绝对路径列表 */
    static QStringList listFiles(const QString &dirPath, const QStringList &suffixes);

    // ===== 版本范围解析辅助 =====
    struct VerSeg
    {
        QList<int> numbers;
        QString prerelease;
        bool valid = false;
    };
    static VerSeg parseVersion(const QString &version);
    static int compareSegments(const VerSeg &a, const VerSeg &b);
    static bool constraintSatisfied(const VerSeg &ver, const QString &constraint);
};

#endif // LOCALCOMPATIBILITYCHECKER_H
