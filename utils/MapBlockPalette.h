/**
 * @file   MapBlockPalette.h
 * @brief  方块 → 地表代表色（俯视图 / 结构立体预览 / 投影平面图共用）
 * @author BlockBox Team
 * @date   2026-11-22
 *
 * 存档地图按方块名取色的调色板。以前只在 WorldMapRenderer 内部使用，
 * 投影/结构的平面图与 3D 预览也要用同一套颜色，抽成独立工具后：
 *   - 存档地图模块（运行时下载）与主程序引用同一份配色，不会出现两套色；
 *   - 主程序里的投影平面图不需要为了一个取色函数把整个地图渲染器带进来。
 */
#ifndef MAPBLOCKPALETTE_H
#define MAPBLOCKPALETTE_H

#include <QColor>
#include <QString>

namespace MapBlockPalette {

/**
 * @brief 方块 ID（含或不含 minecraft: 前缀）→ 地表代表色
 * @param blockName 方块名，如 "minecraft:grass_block" / "grass_block"
 */
QColor blockColor(const QString &blockName);

} // namespace MapBlockPalette

#endif // MAPBLOCKPALETTE_H
