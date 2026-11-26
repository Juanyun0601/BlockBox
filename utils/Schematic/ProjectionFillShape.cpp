/**
 * @file   ProjectionFillShape.cpp
 * @brief  投影编辑器「填充方块」的形状几何实现（纯计算，无 UI 依赖）
 * @author BlockBox Team
 * @date   2026-11-25
 *
 * 所有生成结果都是相对「范围最小角」的整数偏移，并且保证：
 *   - 落在 [0,dx) x [0,dy) x [0,dz) 内；
 *   - 同一次调用里互不重复；
 *   - 任一维度 <= 0 或者形状非法时返回空列表。
 *
 * 这里的函数只做几何换算，既不访问文档也不碰界面；真正把格子写进
 * 投影文档、生成撤销记录的工作由投影编辑器对话框负责。
 */
#include "ProjectionFillShape.h"

#include <QtGlobal>

namespace ProjectionFillShapes
{
namespace
{
    /** 某一维上「贴着边界」的坐标取值个数（长度为 1 时首尾是同一格） */
    int boundaryValueCount(int extent)
    {
        return extent <= 1 ? 1 : 2;
    }

    /** 某一维上完全处于内部的坐标个数（跨度 1、2 时为 0） */
    int interiorValueCount(int extent)
    {
        return extent > 2 ? extent - 2 : 0;
    }

    /** 该坐标是否在边界上（首格或末格） */
    bool isBoundary(int value, int extent)
    {
        return value <= 0 || value >= extent - 1;
    }

    /** 整个范围的格子总数 */
    qsizetype boxVolume(int dx, int dy, int dz)
    {
        return static_cast<qsizetype>(dx) * dy * dz;
    }

    /** 空心壳的格子数：整个范围减去内部余下的「挖空」部分 */
    qsizetype hollowVolume(int dx, int dy, int dz)
    {
        const qsizetype inner = static_cast<qsizetype>(interiorValueCount(dx))
                * interiorValueCount(dy) * interiorValueCount(dz);
        return boxVolume(dx, dy, dz) - inner;
    }

    /** 框架的格子数：至少两个坐标落在边界上（12 条棱、角落各算一次） */
    qsizetype frameVolume(int dx, int dy, int dz)
    {
        const qsizetype bx = boundaryValueCount(dx);
        const qsizetype by = boundaryValueCount(dy);
        const qsizetype bz = boundaryValueCount(dz);
        const qsizetype ix = interiorValueCount(dx);
        const qsizetype iy = interiorValueCount(dy);
        const qsizetype iz = interiorValueCount(dz);

        // 恰好两个坐标在边界上 + 三个坐标都在边界上（8 个角）
        return bx * by * iz + bx * bz * iy + by * bz * ix + bx * by * bz;
    }

    /** 底部内接板的公共定位：边长 s 与左上角 (x0, z0) */
    struct InsetBoard
    {
        int side = 0;
        int startX = 0;
        int startZ = 0;
    };

    InsetBoard insetBoardOf(int dx, int dz)
    {
        InsetBoard board;
        board.side   = qMin(dx, dz);
        board.startX = (dx - board.side) / 2;
        board.startZ = (dz - board.side) / 2;
        return board;
    }

    /**
     * @brief 把一个圆盘追加到结果里
     * @param y 圆盘所在的层
     *
     * 圆心取「底部内接方形板」的中心 startX + (s-1)/2、startZ + (s-1)/2，
     * 半径取短边的一半 s/2。这样圆形与方形/三角形永远同框对齐（dx==dz 时
     * 圆心即范围中心），直径也不会超出 s×s 的内接板；只保留圆心落在整数
     * 格点上的格子，结果天然落在范围之内，无需再裁剪。
     */
    void appendCircleDisc(QVector<Cell> &out, int dx, int dz, int y)
    {
        const InsetBoard board = insetBoardOf(dx, dz);
        const double centerX = board.startX + (board.side - 1) / 2.0;
        const double centerZ = board.startZ + (board.side - 1) / 2.0;
        const double radius  = board.side / 2.0;
        // 加一点点容差，避免 (r 恰好落在格点上) 时被浮点误差吃掉
        const double radiusSq = radius * radius + 1e-9;

        for (int z = 0; z < dz; ++z)
        {
            const double offsetZ = z - centerZ;
            const double offsetZSq = offsetZ * offsetZ;
            for (int x = 0; x < dx; ++x)
            {
                const double offsetX = x - centerX;
                if (offsetX * offsetX + offsetZSq <= radiusSq)
                    out.append(Cell{x, y, z});
            }
        }
    }

} // namespace

QVector<Cell> cells(ProjectionFillShape shape, int dx, int dy, int dz)
{
    QVector<Cell> result;

    // 空范围（任一维度 <= 0）没有任何格子
    if (dx <= 0 || dy <= 0 || dz <= 0)
        return result;

    switch (shape)
    {
    case ProjectionFillShape::Solid:
    {
        result.reserve(boxVolume(dx, dy, dz));
        for (int y = 0; y < dy; ++y)
            for (int z = 0; z < dz; ++z)
                for (int x = 0; x < dx; ++x)
                    result.append(Cell{x, y, z});
        break;
    }

    case ProjectionFillShape::Hollow:
    {
        // 一格厚的外壳：任一坐标贴到边界就保留
        result.reserve(hollowVolume(dx, dy, dz));
        for (int y = 0; y < dy; ++y)
            for (int z = 0; z < dz; ++z)
                for (int x = 0; x < dx; ++x)
                    if (isBoundary(x, dx) || isBoundary(y, dy) || isBoundary(z, dz))
                        result.append(Cell{x, y, z});
        break;
    }

    case ProjectionFillShape::Frame:
    {
        // 12 条棱：至少两个坐标贴到边界
        result.reserve(frameVolume(dx, dy, dz));
        for (int y = 0; y < dy; ++y)
            for (int z = 0; z < dz; ++z)
                for (int x = 0; x < dx; ++x)
                {
                    const int boundaryAxes = (isBoundary(x, dx) ? 1 : 0)
                            + (isBoundary(y, dy) ? 1 : 0)
                            + (isBoundary(z, dz) ? 1 : 0);
                    if (boundaryAxes >= 2)
                        result.append(Cell{x, y, z});
                }
        break;
    }

    case ProjectionFillShape::SlantedFloor:
    {
        // 每个 (x,z) 柱子恰好一格：沿较长的水平轴线性抬升，
        // 跨度只有 1 的轴不产生坡度（整层平铺在 y=0）
        const bool alongX      = dx >= dz;
        const int  slopeLength = alongX ? dx : dz;
        const int  topY        = dy - 1;
        const bool sloped      = slopeLength > 1 && topY > 0;

        result.reserve(static_cast<qsizetype>(dx) * dz);
        for (int z = 0; z < dz; ++z)
        {
            for (int x = 0; x < dx; ++x)
            {
                int y = 0;
                if (sloped)
                {
                    const int position = alongX ? x : z;
                    const double t = static_cast<double>(position)
                            / static_cast<double>(slopeLength - 1);
                    y = qBound(0, qRound(topY * t), topY);
                }
                result.append(Cell{x, y, z});
            }
        }
        break;
    }

    case ProjectionFillShape::Square:
    {
        // 底部内接方形板
        const InsetBoard board = insetBoardOf(dx, dz);
        result.reserve(static_cast<qsizetype>(board.side) * board.side);
        for (int k = 0; k < board.side; ++k)
            for (int i = 0; i < board.side; ++i)
                result.append(Cell{board.startX + i, 0, board.startZ + k});
        break;
    }

    case ProjectionFillShape::Triangle:
    {
        // 底部内接直角三角板：i + k <= s - 1
        const InsetBoard board = insetBoardOf(dx, dz);
        const qsizetype side = board.side;
        result.reserve(side * (side + 1) / 2);
        for (int k = 0; k < board.side; ++k)
            for (int i = 0; i + k < board.side; ++i)
                result.append(Cell{board.startX + i, 0, board.startZ + k});
        break;
    }

    case ProjectionFillShape::Circle:
    {
        // 底部内接圆形板
        result.reserve(static_cast<qsizetype>(dx) * dz);
        appendCircleDisc(result, dx, dz, 0);
        break;
    }

    case ProjectionFillShape::Cylinder:
    {
        // 圆形底面在每一层重复，形成实心圆柱
        QVector<Cell> disc;
        disc.reserve(static_cast<qsizetype>(dx) * dz);
        appendCircleDisc(disc, dx, dz, 0);

        result.reserve(disc.size() * dy);
        for (int y = 0; y < dy; ++y)
            for (const Cell &cell : disc)
                result.append(Cell{cell.x, y, cell.z});
        break;
    }

    case ProjectionFillShape::Count:
    default:
        // 非法形状：返回空，交给调用方按「没有可选格子」处理
        break;
    }

    return result;
}

QString displayName(ProjectionFillShape shape)
{
    switch (shape)
    {
    case ProjectionFillShape::Solid:        return QStringLiteral("实心");
    case ProjectionFillShape::Hollow:       return QStringLiteral("空心");
    case ProjectionFillShape::Frame:        return QStringLiteral("框架");
    case ProjectionFillShape::SlantedFloor: return QStringLiteral("倾斜地板");
    case ProjectionFillShape::Square:       return QStringLiteral("方形");
    case ProjectionFillShape::Triangle:     return QStringLiteral("三角形");
    case ProjectionFillShape::Circle:       return QStringLiteral("圆形");
    case ProjectionFillShape::Cylinder:     return QStringLiteral("圆柱");
    case ProjectionFillShape::Count:
    default:                                return QString();
    }
}

QString description(ProjectionFillShape shape)
{
    switch (shape)
    {
    case ProjectionFillShape::Solid:
        return QStringLiteral("把选定范围整块填满");
    case ProjectionFillShape::Hollow:
        return QStringLiteral("只留一格厚的外壳，内部保持原样");
    case ProjectionFillShape::Frame:
        return QStringLiteral("只沿 12 条棱边填充，像方框骨架");
    case ProjectionFillShape::SlantedFloor:
        return QStringLiteral("每个柱子铺一格，沿长边由低到高形成坡面");
    case ProjectionFillShape::Square:
        return QStringLiteral("在底面铺一块居中的方形板");
    case ProjectionFillShape::Triangle:
        return QStringLiteral("在底面铺一块居中的直角三角形板");
    case ProjectionFillShape::Circle:
        return QStringLiteral("在底面铺一块居中的圆形板");
    case ProjectionFillShape::Cylinder:
        return QStringLiteral("以圆形底面为准，向上立起实心圆柱");
    case ProjectionFillShape::Count:
    default:
        return QString();
    }
}

int groupOf(ProjectionFillShape shape)
{
    switch (shape)
    {
    case ProjectionFillShape::Solid:
    case ProjectionFillShape::Hollow:
    case ProjectionFillShape::Frame:
    case ProjectionFillShape::SlantedFloor:
        return 0; // 方块填充

    case ProjectionFillShape::Square:
    case ProjectionFillShape::Triangle:
    case ProjectionFillShape::Circle:
    case ProjectionFillShape::Cylinder:
        return 1; // 平面与立体形状

    case ProjectionFillShape::Count:
    default:
        return -1; // 非法形状不属于任何分组
    }
}

} // namespace ProjectionFillShapes
