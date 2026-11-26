/**
 * @file   GameAchievementCatalog.h
 * @brief  游戏成就的展示信息：标题、对应达成条件、获得难度
 * @date   2026-11-22
 *
 * 勋章墙只显示已经获得的游戏成就；这里给常见原版里程碑补上更准确的
 * 条件和难度标签，没匹配到的模组成就继续用游戏同步上来的标题 / 条件。
 * 图标沿用 MedalCatalog 的五大分类图标。
 */
#pragma once

#include <QColor>
#include <QString>
#include <QVector>

struct GameAchievementDef
{
    QString id;         ///< 原版 advancement id，例如 minecraft:story/mine_stone
    QString name;       ///< 游戏内中文标题
    QString condition;  ///< 达成条件文案
    QString difficulty; ///< 获得难度：简单 / 普通 / 困难 / 极难
};

namespace GameAchievementCatalog {

/** 固定展示顺序：主线 → 下界 → 末地 → 冒险 → 农牧 */
const QVector<GameAchievementDef> &all();

/** 按 advancement id 或中文标题匹配定义；找不到返回 nullptr */
const GameAchievementDef *find(const QString &idOrTitle);

/** 已知成就的获得难度；未知的模组成就按「普通」算 */
QString difficultyForId(const QString &id);

/** 难度对应的标签颜色 */
QColor difficultyColor(const QString &difficulty);

} // namespace GameAchievementCatalog
