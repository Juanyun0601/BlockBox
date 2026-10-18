/**
 * @file   SeedTerrainRenderer.h
 * @brief  未加载区块的种子预测地形渲染（cubiomes 生物群系 + 近似地表高度）
 * @author BlockBox Team
 * @date   2026-10-15
 *
 * 存档里还没生成的区块没有 region 文件，地图上原本只能留白。本模块用
 * cubiomes 按世界种子预测这些区域的生物群系与近似地表高度，渲染成与
 * 真实地表图同风格的俯视预览（同款方块配色 + 高度明暗 + 坡度阴影）。
 *
 * 注意：这是**预测预览**，不是真实地形 —— 高度来自 cubiomes 的近似地表
 * 估算，实际地形（含结构、洞穴、建筑）以游戏内为准。
 *
 * 线程模型：与 WorldMapRenderer 相同，接口为无状态纯函数（内部 Generator
 * 按线程缓存），可直接放工作线程执行。
 */

#ifndef SEEDTERRAINRENDERER_H
#define SEEDTERRAINRENDERER_H

#include <QImage>

#include "WorldMapRenderer.h"

namespace SeedTerrainRenderer {

/**
 * 该版本 + 维度能否做种子预测。
 * mc 为 StructureFinder::versionToInt() 的结果（0 = 读不出版本 → false）。
 */
bool supported(int mc, WorldMapRenderer::Dimension dim);

/**
 * @brief 渲染单个 region 的种子预测图（预览）
 *
 * @param regionX,regionZ region 坐标（与 r.X.Z.mca 一致）
 * @param dim     维度
 * @param mode    着色方式（与真实地表图一致：方块色 / 高度灰度 / 地形图）
 * @param seed    世界种子
 * @param mc      cubiomes 版本号（versionToInt 的结果）
 * @param outSize 输出边长（像素）。概览降采样时传 512/bpp（内部采样封顶 128）；
 *                1:1 细节瓦片传 512 → 自动升到方块级（scale=1 群系 + 连续高度），
 *                精确到单个方块。采样恒定铺满整个 region，绝不只采一角再拉伸
 * @param maxSurfaceY 高度上限（-1 = 不限制）。设定后与真实地表同语义：预测
 *                地表高于该 Y 时截断成固体（石头 / 下界岩 / 末地石）；海洋
 *                水底低于截断面时仍显示水色，水面下沉到截断 Y 并按剩余水深
 *                调暗
 * @return outSize×outSize 的 ARGB 图；不支持的组合返回空图
 */
QImage renderRegionTile(int regionX, int regionZ,
                        WorldMapRenderer::Dimension dim,
                        WorldMapRenderer::ShadeMode mode,
                        quint64 seed, int mc, int outSize = 512,
                        int maxSurfaceY = -1);

} // namespace SeedTerrainRenderer

#endif // SEEDTERRAINRENDERER_H
