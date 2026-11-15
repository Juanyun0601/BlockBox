/**
 * @file   LobbyInstanceMatcher.h
 * @brief  联机大厅用：扫描本地实例，给房间挑一个「版本 / 加载器 / 模组 / 资源包」都合适的实例
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 大厅房间会带上房主用的游戏版本、加载器、模组（其中一部分带「辅助」标签）、
 * 资源包和光影包。加入时按下面的顺序打分：
 *   版本一致 → 加载器一致 → 必装模组齐全（辅助模组算加分不算门槛）→ 资源包 / 光影包对得上
 * 分数最高的那个实例胜出；同分时选最近改动过的，这样「一键加入」总是落在最新、最全的实例上。
 *
 * 速度：扫描结果带两级缓存（实例根目录时间戳 + 每个实例的 JSON / mods / 资源包时间戳），
 * 房间列表里每张卡都要问「本机哪个实例合适」，没有缓存时每次都要重新读盘，有缓存后
 * 只在文件真的变了以后才重扫。首次进大厅可以调 warmUp() 先把扫描跑完，点进去就是现成的。
 */

#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

/** 扫描到的本地实例（一个 versions/<id> 目录算一个） */
struct LobbyInstanceInfo
{
    QString path;             ///< 实例根目录（versions/<id>）
    QString id;               ///< 实例目录名
    QString version;          ///< 游戏版本，例如 1.20.1
    QString loader;           ///< Vanilla / Forge / Fabric / NeoForge / Quilt
    QStringList mods;         ///< mods 目录里的 jar 文件名
    QStringList resourcePacks;///< resourcepacks 目录里的文件名
    QStringList shaderPacks;  ///< shaderpacks 目录里的文件名
    QDateTime modifiedAt;     ///< 版本 JSON 的最近修改时间（用来判断「最新」）

    /** 大小写不敏感的查表集合（打分时用，避免反复构造） */
    QSet<QString> lowerMods;
    QSet<QString> lowerResourcePacks;
    QSet<QString> lowerShaderPacks;

    /** 把上面三个列表整理成小写集合（扫描后自动调用，外部手填实例时也要调） */
    void buildIndex();
};

namespace LobbyInstanceMatcher
{
/** 匹配等级；数值越大越合适 */
enum class MatchLevel
{
    None = 0,
    Version = 1,
    Loader = 2,
    Mods = 3
};

/** 房间对本地实例的要求（由 ForumLobbyRoom 抽出来，匹配器不依赖论坛结构） */
struct Requirement
{
    QString version;          ///< 要求的游戏版本，空 = 不限
    QString loader;           ///< 要求的加载器，空 = 不限
    QStringList requiredMods; ///< 必装模组（房主模组里去掉辅助标签后的部分）
    QStringList auxMods;      ///< 带「辅助」标签的模组：缺了不影响进房，只做加分
    QStringList resourcePacks;///< 房主的资源包
    QStringList shaderPacks;  ///< 房主的光影包

    bool isEmpty() const
    {
        return version.trimmed().isEmpty() && loader.trimmed().isEmpty()
               && requiredMods.isEmpty();
    }
};

/** 一个实例与房间要求的比较结果 */
struct InstanceCheck
{
    LobbyInstanceInfo instance;
    MatchLevel level = MatchLevel::None;
    int score = 0;                    ///< 综合得分，越大越合适
    QStringList missingMods;          ///< 必装但本机没有的模组
    QStringList missingAuxMods;       ///< 辅助模组里本机没有的
    QStringList missingResourcePacks; ///< 本机没有的资源包
    QStringList missingShaderPacks;   ///< 本机没有的光影包

    int missingTotal() const
    {
        return missingMods.size() + missingAuxMods.size() + missingResourcePacks.size()
               + missingShaderPacks.size();
    }
    bool hasAnythingToFetch() const { return missingTotal() > 0; }
};

/** 「一键加入」准备计划：挑中的实例 + 需要补装的内容 */
struct JoinPlan
{
    bool found = false;              ///< 本机有没有能用的实例
    InstanceCheck best;              ///< 挑中的实例（found 为 false 时是空结构）
    QList<InstanceCheck> candidates; ///< 备选实例（含 best，按得分从高到低，最多 8 个）
    Requirement requirement;         ///< 房间要求（界面直接展示）
    QString installHint;             ///< 需要先装实例时的说明（found 为 false 才有）
};

/** 实例 mods 目录里的 jar 文件名（没有目录返回空） */
QStringList scanMods(const QString &instancePath);

/** 实例 resourcepacks / shaderpacks 目录里的文件名 */
QStringList scanResourcePacks(const QString &instancePath);
QStringList scanShaderPacks(const QString &instancePath);

/**
 * @brief 扫描所有已配置实例文件夹里的实例（带缓存）
 * @param forceRefresh true 时无视缓存重新扫盘（用户手动刷新实例列表后用）
 */
QList<LobbyInstanceInfo> scanInstances(bool forceRefresh = false);

/**
 * @brief 预扫描：首次进联机大厅 / 大厅详情页前先跑一遍，后面点「一键加入」就不用等
 *
 * 只做一次扫描，扫完把结果留在缓存里；重复调用很便宜（只比时间戳）。
 */
void warmUp();

/** 清空缓存（实例目录增删、安装 / 删除实例后调用） */
void invalidateCache();

/** 加载器名归一化：空 / Vanilla / 原版 都当作同一个 */
QString normalizeLoader(const QString &loader);

/** 房间对实例的要求：loader 归一化，版本 / 模组去空白 */
Requirement requirementFor(const QString &version, const QString &loader,
                           const QStringList &requiredMods,
                           const QStringList &auxMods = QStringList(),
                           const QStringList &resourcePacks = QStringList(),
                           const QStringList &shaderPacks = QStringList());

/**
 * @brief 给这个实例和房间打分
 * @param missingMods 非空时写入缺少的必装模组文件名
 */
MatchLevel evaluate(const LobbyInstanceInfo &instance, const QString &version,
                    const QString &loader, const QStringList &mods,
                    QStringList *missingMods = nullptr);

/** 完整比较：等级 + 得分 + 各类缺失内容 */
InstanceCheck compare(const LobbyInstanceInfo &instance, const Requirement &requirement);

/**
 * @brief 挑一个最合适的实例
 * @param version       房间要求的版本，空表示不限
 * @param loader        房间要求的加载器，空表示不限
 * @param mods          房间要求的模组（完整列表，兼容旧调用）
 * @param best          输出：挑中的实例
 * @param level         输出：匹配等级
 * @param missingMods   输出：挑中实例缺少的模组
 * @param pool          可选：直接传已扫描好的列表，避免重复扫盘
 * @return 找到可用实例返回 true
 */
bool pickBest(const QString &version, const QString &loader, const QStringList &mods,
              LobbyInstanceInfo *best, MatchLevel *level = nullptr,
              QStringList *missingMods = nullptr,
              const QList<LobbyInstanceInfo> &pool = QList<LobbyInstanceInfo>());

/**
 * @brief 生成「一键加入」计划：挑实例 + 列出要补装的内容
 * @param requirement 房间要求
 * @param pool        可选：已扫描好的实例列表
 */
JoinPlan makePlan(const Requirement &requirement,
                  const QList<LobbyInstanceInfo> &pool = QList<LobbyInstanceInfo>());

/** 匹配等级的中文说法，界面上直接显示 */
QString levelText(MatchLevel level);
} // namespace LobbyInstanceMatcher
