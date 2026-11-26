/**
 * @file   ProjectionFillShape.h
 * @brief  投影编辑器「填充方块」的形状几何（纯计算，无 UI 依赖）
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 投影编辑器里点「填充方块」后会先选一个形状，再在视图里点两个对角方块，
 * 两点围出的长方体就是形状的范围。本模块只负责把这个范围换算成一格格
 * 相对坐标（0 起），真正的写入与撤销由编辑器对话框完成。
 *
 * 形状分两组：
 *   - 方块填充：实心 / 空心 / 框架 / 倾斜地板
 *   - 平面与立体形状：方形 / 三角形 / 圆形 / 圆柱
 */
#pragma once

#include <QString>
#include <QVector>

/**
 * @brief 填充形状
 */
enum class ProjectionFillShape : int
{
    Solid = 0,     ///< 实心：整个范围填满
    Hollow,        ///< 空心：只留外表面一格厚的壳
    Frame,         ///< 框架：只留 12 条棱边
    SlantedFloor,  ///< 倾斜地板：逐层升高的坡面
    Square,        ///< 方形：底部内接方形板
    Triangle,      ///< 三角形：底部内接直角三角板
    Circle,        ///< 圆形：底部内接圆形板
    Cylinder,      ///< 圆柱：范围内立起实心圆柱
    Count          ///< 形状数量（遍历用）
};

namespace ProjectionFillShapes
{
    /**
     * @brief 相对范围最小角的一格偏移
     */
    struct Cell
    {
        int x = 0;
        int y = 0;
        int z = 0;
    };

    /**
     * @brief 生成形状占用的全部格子（相对范围最小角的偏移）
     * @param shape 形状
     * @param dx/dy/dz 范围三个方向的跨度（>=1 才有格子）
     * @return 互不重复、且满足 0 <= x < dx 等边界约束的格子；
     *         任一维度 <= 0 或形状非法（如 Count）时返回空列表
     */
    QVector<Cell> cells(ProjectionFillShape shape, int dx, int dy, int dz);

    /** 形状中文名（实心 / 空心 / 框架 / 倾斜地板 / 方形 / 三角形 / 圆形 / 圆柱） */
    QString displayName(ProjectionFillShape shape);

    /** 形状的一句话说明（弹窗里显示在形状名称下方） */
    QString description(ProjectionFillShape shape);

    /** 形状分组：0 = 方块填充，1 = 平面与立体形状；非法形状（如 Count）返回 -1 */
    int groupOf(ProjectionFillShape shape);
} // namespace ProjectionFillShapes
