/**
 * @file   LobbyInstanceMatcher.h
 * @brief  联机大厅用：扫描本地实例，给房间挑一个「版本 / 加载器 / 模组」都合适的实例
 * @author BlockBox Team
 * @date   2026-11-01
 *
 * 大厅房间会带上房主用的游戏版本、加载器和模组文件名。加入时按下面的顺序打分：
 *   3 = 模组也齐全（模组列表比得上）
 *   2 = 版本 + 加载器一致
 *   1 = 只有版本一致
 *   0 = 版本都对不上，不能用
 * 同分时选最近修改过的那个实例，这样「一键加入」总是落在最新的合适实例上。
 */

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

/** 扫描到的本地实例（一个 versions/<id> 目录算一个） */
struct LobbyInstanceInfo
{
    QString path;        ///< 实例根目录（versions/<id>）
    QString id;          ///< 实例目录名
    QString version;     ///< 游戏版本，例如 1.20.1
    QString loader;      ///< Vanilla / Forge / Fabric / NeoForge / Quilt
    QStringList mods;    ///< mods 目录里的 jar 文件名
    QDateTime modifiedAt;///< 版本 JSON 的最近修改时间（用来判断「最新」）
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

/** 实例 mods 目录里的 jar 文件名（没有目录返回空） */
QStringList scanMods(const QString &instancePath);

/** 扫描所有已配置实例文件夹里的实例 */
QList<LobbyInstanceInfo> scanInstances();

/** 加载器名归一化：空 / Vanilla / 原版 都当作同一个 */
QString normalizeLoader(const QString &loader);

/**
 * @brief 给这个实例和房间打分
 * @param missingMods 非空时写入缺少的模组文件名
 */
MatchLevel evaluate(const LobbyInstanceInfo &instance, const QString &version,
                    const QString &loader, const QStringList &mods,
                    QStringList *missingMods = nullptr);

/**
 * @brief 挑一个最合适的实例
 * @param version       房间要求的版本，空表示不限
 * @param loader        房间要求的加载器，空表示不限
 * @param mods          房间要求的模组
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

/** 匹配等级的中文说法，界面上直接显示 */
QString levelText(MatchLevel level);
}
