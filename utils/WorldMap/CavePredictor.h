/**
 * @file   CavePredictor.h
 * @brief  未加载区块的地下洞穴预测（1.18+ 主世界噪声洞穴复刻）
 * @author BlockBox Team
 * @date   2026-10-25
 *
 * 存档地图设置高度切片后，未生成区块原本只能整片画成实心石头。
 * 本模块按世界种子复刻原版 1.18+ 的洞穴密度函数（cheese 大洞 /
 * spaghetti 双向隧道 / noodle 细洞 / entrances 入口 / 石柱回填），
 * 预测切片高度上哪些位置被洞穴掏空。
 *
 * 公式与常量取自原版客户端 worldgen 数据（entrances / spaghetti_2d /
 * noodle / pillars 等密度函数 JSON 与噪声参数），种子推导沿用 cubiomes
 * 的 MD5(噪声名) XOR 世界种子方案。与原版逐方块结果的吻合度用原生
 * 1.21.4 服务端生成的存档实测：洞穴召回约 75%，边界整体外扩约 2~3 格
 * （含水层屏障墙等未建模项），作为"近似预览"使用。
 *
 * 线程模型：与 WorldMapRenderer 相同，接口为无状态纯函数，噪声按线程
 * 缓存，可直接放工作线程执行。
 */

#ifndef CAVEPREDICTOR_H
#define CAVEPREDICTOR_H

#include <QtGlobal>

namespace CavePredictor {

/** 预测的地下内容：实心 / 洞穴（空气）/ 岩浆 */
enum class Cell
{
    Solid = 0,
    Cave = 1,
    Lava = 2
};

/** 1.18 及以上主世界可用（旧版为蠕虫洞随机雕刻，噪声公式不适用） */
bool supported(int mc);

/**
 * @brief 预测 (x, y, z) 的地下内容
 *
 * @param seed     世界种子（决定噪声场）
 * @param x,y,z    方块坐标；y 为切片高度
 * @param surfaceY 该列近似地表高度（密度函数里的斜坡奶酪项需要它
 *                 决定隧道近地表段是否收窄），预测无效时传 0 也能用，
 *                 但近地表几十格内的边界会偏宽
 * @return 洞穴按噪声密度判定（近似）；y 低于 -54 的洞按原版规则必为
 *         岩浆（含水层岩浆层，精确）；洞穴水（局部含水层）预测位置
 *         不可靠，暂不输出
 */
Cell sampleAt(quint64 seed, int x, int y, int z, double surfaceY);

/** 便捷判定：是否被掏空（洞穴或岩浆） */
inline bool carvedAt(quint64 seed, int x, int y, int z, double surfaceY)
{
    return sampleAt(seed, x, y, z, surfaceY) != Cell::Solid;
}

} // namespace CavePredictor

#endif // CAVEPREDICTOR_H
