/**
 * @file   StructureModelBuilder.h
 * @brief  按世界种子程序化构建结构的三维方块模型（结构详情「预览」页用）
 * @author BlockBox Team
 * @date   2026-10-26
 *
 * 说明：预览页展示的是「按种子重建的结构主体」。冰屋、沉船、村庄等
 * 14 类结构直接按原版结构模板逐块还原；村庄另外完整复刻了原版拼图
 * （JigsawPlacement）算法——随机源、模板池抽取、朝向与段间拼接都按
 * 1.21.4 的实现还原，同一世界种子算出的村庄与游戏内实际生成的一致
 * （苔藓、路面风化这类处理器随机装饰未包含）。其余无模板可依的结构
 * 仍按原版方块构成程序化近似。
 *
 * 生成的模型交给 SchematicDocument，由 VoxelViewWidget（投影预览同一
 * 套渲染引擎）绘制，贴图/形状取自当前实例的客户端资源。
 */

#ifndef STRUCTUREMODELBUILDER_H
#define STRUCTUREMODELBUILDER_H

#include <QString>

class SchematicDocument;

/** 生成结果概要（尺寸 / 方块数 / 变体说明） */
struct StructureModelInfo
{
    int sizeX = 0;        ///< 模型宽（X）
    int sizeY = 0;        ///< 模型高（Y）
    int sizeZ = 0;        ///< 模型深（Z）
    int blockCount = 0;   ///< 非空气方块数
    int rotation = 0;     ///< 种子决定的水平旋转（0~3，每档 90°）
    QString variant;      ///< 变体说明（如「平原村庄」「搁浅沉船」）
    QString error;        ///< 非空表示未生成（结构不支持 / 无种子 / 内存不足）
    bool fromVanillaTemplate = false; ///< true：由原版结构模板逐块还原
};

/**
 * @brief 结构模型构建器
 */
class StructureModelBuilder
{
public:
    /** 该结构是否有可用的三维模型 */
    static bool supports(const QString &structKey);

    /**
     * @brief 按种子生成结构模型
     * @param structKey  StructureFinder 的结构 key
     * @param x,z        结构方块坐标（参与种子混合，决定朝向与变体）
     * @param seed       世界种子
     * @param hasSeed    是否读到世界种子
     * @param mcVersion  游戏版本（如 "1.21.4"；用于避开新版本方块）
     * @param info       输出概要（可为空）
     * @return 新建文档，调用方负责 delete；失败返回 nullptr 并在 info 写原因
     */
    static SchematicDocument *build(const QString &structKey, qint64 x, qint64 z,
                                    quint64 seed, bool hasSeed,
                                    const QString &mcVersion,
                                    StructureModelInfo *info = nullptr);
};

#endif // STRUCTUREMODELBUILDER_H
