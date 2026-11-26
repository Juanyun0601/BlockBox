/**
 * @file   main.cpp
 * @brief  ProjectionFillShape 独立探针：核对 8 种填充形状的格子生成
 *
 * 不依赖 QTest，也不链接主工程：直接用 g++ 编译本文件 +
 * utils/Schematic/ProjectionFillShape.cpp + Qt6Core 即可运行。
 *
 * 覆盖内容：
 *   1) 越界 / 重复 / 维度 <= 0 返回空 / 非法形状返回空；
 *   2) 各形状数量公式（实心、空心、框架、倾斜地板、方形、三角形、圆形、圆柱）；
 *   3) 语义细节：倾斜地板每柱恰一格且沿坡轴单调、三角 i+k<=s-1、
 *      方形/圆形的底部定位、圆柱逐层重复；
 *   4) 已知值：圆形 s=5 -> 21 格、s=4 -> 12 格、框架 3x3x3 -> 20 格；
 *   5) 显示名称 / 说明 / 分组映射。
 *
 * 任何一项不符都会打印明细，并让进程以非 0 退出。
 */
#include "utils/Schematic/ProjectionFillShape.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using ProjectionFillShapes::Cell;

namespace
{
    // ---------------------------------------------------------------- 测试台账
    int g_checks = 0;
    int g_failures = 0;

    struct Dim
    {
        int dx = 0;
        int dy = 0;
        int dz = 0;
    };

    struct ShapeCase
    {
        ProjectionFillShape shape;
        const char *name; // 控制台里的英文名，避免中文编码干扰
    };

    const ShapeCase kShapes[] = {
        {ProjectionFillShape::Solid, "Solid"},
        {ProjectionFillShape::Hollow, "Hollow"},
        {ProjectionFillShape::Frame, "Frame"},
        {ProjectionFillShape::SlantedFloor, "SlantedFloor"},
        {ProjectionFillShape::Square, "Square"},
        {ProjectionFillShape::Triangle, "Triangle"},
        {ProjectionFillShape::Circle, "Circle"},
        {ProjectionFillShape::Cylinder, "Cylinder"},
    };

    // 任务要求覆盖的尺寸
    const Dim kRequiredDims[] = {
        {1, 1, 1}, {2, 7, 4}, {3, 1, 3}, {3, 3, 3}, {5, 5, 5}, {10, 3, 7},
    };

    const char *shapeName(ProjectionFillShape shape)
    {
        for (const ShapeCase &c : kShapes)
            if (c.shape == shape)
                return c.name;
        return "?";
    }

    std::string dimText(const Dim &d)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "(%d,%d,%d)", d.dx, d.dy, d.dz);
        return buffer;
    }

    std::string cellText(const Cell &c)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "{%d,%d,%d}", c.x, c.y, c.z);
        return buffer;
    }

    void check(bool ok, const Dim &d, ProjectionFillShape shape, const std::string &what)
    {
        ++g_checks;
        if (ok)
            return;
        ++g_failures;
        std::printf("  FAIL  %-12s %-10s %s\n", shapeName(shape), dimText(d).c_str(), what.c_str());
    }

    std::size_t cellIndex(const Dim &d, int x, int y, int z)
    {
        return (static_cast<std::size_t>(x) * d.dy + y) * d.dz + z;
    }

    // ------------------------------------------------- 独立重算（不复用被测实现）

    long long boxVolume(const Dim &d)
    {
        return static_cast<long long>(d.dx) * d.dy * d.dz;
    }

    bool onBoundary(int value, int extent)
    {
        return value <= 0 || value >= extent - 1;
    }

    /**
     * 圆盘与「底部内接方形板」同框：圆心取方板中心 startX + (s-1)/2、
     * startZ + (s-1)/2，半径取短边一半 s/2。
     * 这里刻意用 startX + (s-1) * 0.5 的写法，与被测实现分开推导。
     */
    bool insideCircle(const Dim &d, int x, int z)
    {
        const int shortSide = d.dx < d.dz ? d.dx : d.dz;
        const int startX = (d.dx - shortSide) / 2;
        const int startZ = (d.dz - shortSide) / 2;
        const double centerX = startX + (shortSide - 1) * 0.5;
        const double centerZ = startZ + (shortSide - 1) * 0.5;
        const double radius = shortSide * 0.5;
        const double ex = x - centerX;
        const double ez = z - centerZ;
        return ex * ex + ez * ez <= radius * radius + 1e-9;
    }

    // 在「内接板局部坐标」里数圆盘点：和实现里的全局坐标循环是两条路径
    long long countCircle(const Dim &d)
    {
        const int shortSide = d.dx < d.dz ? d.dx : d.dz;
        const double centerOffset = (shortSide - 1) * 0.5;
        const double radiusSq = (shortSide * 0.5) * (shortSide * 0.5);
        long long n = 0;
        for (int k = 0; k < shortSide; ++k)
            for (int i = 0; i < shortSide; ++i)
            {
                const double ex = i - centerOffset;
                const double ez = k - centerOffset;
                if (ex * ex + ez * ez <= radiusSq + 1e-9)
                    ++n;
            }
        return n;
    }

    bool atLeastTwoAxesOnBoundary(const Dim &d, const Cell &c)
    {
        const int axes = (onBoundary(c.x, d.dx) ? 1 : 0)
                + (onBoundary(c.y, d.dy) ? 1 : 0)
                + (onBoundary(c.z, d.dz) ? 1 : 0);
        return axes >= 2;
    }

    // 倾斜地板的高度：独立用 floor(x + 0.5) 重算，不依赖库里的 qRound
    int slantHeight(const Dim &d, int x, int z)
    {
        const bool alongX = d.dx >= d.dz;
        const int slopeLength = alongX ? d.dx : d.dz;
        const int topY = d.dy - 1;
        if (slopeLength <= 1 || topY <= 0)
            return 0;
        const int position = alongX ? x : z;
        const double t = static_cast<double>(position) / static_cast<double>(slopeLength - 1);
        return static_cast<int>(std::floor(topY * t + 0.5));
    }

    // 逐格属性重算的期望格子集合（与被测实现走不同的代码路径）
    std::vector<char> expectedMask(ProjectionFillShape shape, const Dim &d)
    {
        std::vector<char> mask(static_cast<std::size_t>(d.dx) * d.dy * d.dz, 0);
        const int shortSide = d.dx < d.dz ? d.dx : d.dz;
        const int startX = (d.dx - shortSide) / 2;
        const int startZ = (d.dz - shortSide) / 2;

        switch (shape)
        {
        case ProjectionFillShape::Solid:
            for (std::size_t i = 0; i < mask.size(); ++i)
                mask[i] = 1;
            break;

        case ProjectionFillShape::Hollow:
            for (int y = 0; y < d.dy; ++y)
                for (int z = 0; z < d.dz; ++z)
                    for (int x = 0; x < d.dx; ++x)
                        if (onBoundary(x, d.dx) || onBoundary(y, d.dy) || onBoundary(z, d.dz))
                            mask[cellIndex(d, x, y, z)] = 1;
            break;

        case ProjectionFillShape::Frame:
            for (int y = 0; y < d.dy; ++y)
                for (int z = 0; z < d.dz; ++z)
                    for (int x = 0; x < d.dx; ++x)
                        if (atLeastTwoAxesOnBoundary(d, Cell{x, y, z}))
                            mask[cellIndex(d, x, y, z)] = 1;
            break;

        case ProjectionFillShape::SlantedFloor:
            for (int z = 0; z < d.dz; ++z)
                for (int x = 0; x < d.dx; ++x)
                    mask[cellIndex(d, x, slantHeight(d, x, z), z)] = 1;
            break;

        case ProjectionFillShape::Square:
            for (int k = 0; k < shortSide; ++k)
                for (int i = 0; i < shortSide; ++i)
                    mask[cellIndex(d, startX + i, 0, startZ + k)] = 1;
            break;

        case ProjectionFillShape::Triangle:
            for (int k = 0; k < shortSide; ++k)
                for (int i = 0; i < shortSide; ++i)
                    if (i + k <= shortSide - 1)
                        mask[cellIndex(d, startX + i, 0, startZ + k)] = 1;
            break;

        case ProjectionFillShape::Circle:
            for (int z = 0; z < d.dz; ++z)
                for (int x = 0; x < d.dx; ++x)
                    if (insideCircle(d, x, z))
                        mask[cellIndex(d, x, 0, z)] = 1;
            break;

        case ProjectionFillShape::Cylinder:
            for (int y = 0; y < d.dy; ++y)
                for (int z = 0; z < d.dz; ++z)
                    for (int x = 0; x < d.dx; ++x)
                        if (insideCircle(d, x, z))
                            mask[cellIndex(d, x, y, z)] = 1;
            break;

        case ProjectionFillShape::Count:
        default:
            break;
        }
        return mask;
    }

    // 各形状期望格子数
    long long expectedCount(ProjectionFillShape shape, const Dim &d)
    {
        const int shortSide = d.dx < d.dz ? d.dx : d.dz;

        switch (shape)
        {
        case ProjectionFillShape::Solid:
            return boxVolume(d);

        case ProjectionFillShape::Hollow:
        {
            const long long innerX = d.dx > 2 ? d.dx - 2 : 0;
            const long long innerY = d.dy > 2 ? d.dy - 2 : 0;
            const long long innerZ = d.dz > 2 ? d.dz - 2 : 0;
            return boxVolume(d) - innerX * innerY * innerZ;
        }

        case ProjectionFillShape::Frame:
        {
            // 逐格统计（不用闭合公式，避免和实现犯同一个错）
            long long n = 0;
            for (int y = 0; y < d.dy; ++y)
                for (int z = 0; z < d.dz; ++z)
                    for (int x = 0; x < d.dx; ++x)
                        if (atLeastTwoAxesOnBoundary(d, Cell{x, y, z}))
                            ++n;
            return n;
        }

        case ProjectionFillShape::SlantedFloor:
            return static_cast<long long>(d.dx) * d.dz;

        case ProjectionFillShape::Square:
            return static_cast<long long>(shortSide) * shortSide;

        case ProjectionFillShape::Triangle:
            return static_cast<long long>(shortSide) * (shortSide + 1) / 2;

        case ProjectionFillShape::Circle:
            return countCircle(d);

        case ProjectionFillShape::Cylinder:
            return countCircle(d) * d.dy;

        case ProjectionFillShape::Count:
        default:
            return 0;
        }
    }

    // ------------------------------------------------------------ 单形状检查

    // 越界 + 重复
    void checkBoundsAndDuplicates(const Dim &d, ProjectionFillShape shape, const QVector<Cell> &got)
    {
        std::vector<char> seen(static_cast<std::size_t>(d.dx) * d.dy * d.dz, 0);
        int outOfBounds = 0;
        int duplicates = 0;
        std::string oobDetail;
        std::string dupDetail;

        for (const Cell &c : got)
        {
            const bool inRange = c.x >= 0 && c.x < d.dx && c.y >= 0 && c.y < d.dy
                    && c.z >= 0 && c.z < d.dz;
            if (!inRange)
            {
                ++outOfBounds;
                if (outOfBounds <= 3)
                    oobDetail += " " + cellText(c);
                continue;
            }
            const std::size_t index = cellIndex(d, c.x, c.y, c.z);
            if (seen[index])
            {
                ++duplicates;
                if (duplicates <= 3)
                    dupDetail += " " + cellText(c);
            }
            seen[index] = 1;
        }

        check(outOfBounds == 0, d, shape,
              "out-of-range cells:" + std::to_string(outOfBounds) + oobDetail);
        check(duplicates == 0, d, shape,
              "duplicate cells:" + std::to_string(duplicates) + dupDetail);
    }

    // 和独立重算的期望集合逐格比对
    void checkAgainstExpectedSet(const Dim &d, ProjectionFillShape shape, const QVector<Cell> &got)
    {
        const std::vector<char> want = expectedMask(shape, d);
        std::vector<char> actual(want.size(), 0);
        for (const Cell &c : got)
        {
            if (c.x < 0 || c.x >= d.dx || c.y < 0 || c.y >= d.dy || c.z < 0 || c.z >= d.dz)
                continue;
            actual[cellIndex(d, c.x, c.y, c.z)] = 1;
        }

        long long missing = 0;
        long long extra = 0;
        std::string missingDetail;
        std::string extraDetail;
        for (int y = 0; y < d.dy; ++y)
            for (int z = 0; z < d.dz; ++z)
                for (int x = 0; x < d.dx; ++x)
                {
                    const std::size_t index = cellIndex(d, x, y, z);
                    if (want[index] && !actual[index])
                    {
                        ++missing;
                        if (missing <= 3)
                            missingDetail += " " + cellText(Cell{x, y, z});
                    }
                    else if (!want[index] && actual[index])
                    {
                        ++extra;
                        if (extra <= 3)
                            extraDetail += " " + cellText(Cell{x, y, z});
                    }
                }

        check(missing == 0, d, shape,
              "missing cells:" + std::to_string(missing) + missingDetail);
        check(extra == 0, d, shape,
              "unexpected cells:" + std::to_string(extra) + extraDetail);
    }

    // 数量公式
    void checkCountFormula(const Dim &d, ProjectionFillShape shape, long long actual)
    {
        const long long want = expectedCount(shape, d);
        check(actual == want, d, shape,
              "count=" + std::to_string(actual) + " expected=" + std::to_string(want));
    }

    // 各形状的语义细节
    void checkShapeSemantics(const Dim &d, ProjectionFillShape shape, const QVector<Cell> &got)
    {
        switch (shape)
        {
        case ProjectionFillShape::SlantedFloor:
        {
            // 每个 (x,z) 柱子恰好一格
            std::vector<int> columnHits(static_cast<std::size_t>(d.dx) * d.dz, 0);
            std::vector<int> columnY(static_cast<std::size_t>(d.dx) * d.dz, -1);
            int columnViolations = 0;
            std::string detail;
            for (const Cell &c : got)
            {
                const std::size_t index = static_cast<std::size_t>(c.x) * d.dz + c.z;
                if (++columnHits[index] > 1)
                {
                    ++columnViolations;
                    if (columnViolations <= 3)
                        detail += " dup(" + std::to_string(c.x) + "," + std::to_string(c.z) + ")";
                    continue;
                }
                columnY[index] = c.y;
            }
            for (int z = 0; z < d.dz; ++z)
                for (int x = 0; x < d.dx; ++x)
                    if (columnHits[static_cast<std::size_t>(x) * d.dz + z] != 1)
                    {
                        ++columnViolations;
                        if (columnViolations <= 3)
                            detail += " column(" + std::to_string(x) + "," + std::to_string(z) + ")";
                    }
            check(columnViolations == 0, d, shape,
                  "column-count violations:" + std::to_string(columnViolations) + detail);

            // 沿坡轴单调不减 + 两端贴到 y=0 / y=dy-1
            const bool alongX = d.dx >= d.dz;
            const int slopeLength = alongX ? d.dx : d.dz;
            int monotoneViolations = 0;
            std::string monoDetail;
            if (alongX)
            {
                for (int z = 0; z < d.dz; ++z)
                    for (int x = 1; x < d.dx; ++x)
                    {
                        const int prev = columnY[static_cast<std::size_t>(x - 1) * d.dz + z];
                        const int cur = columnY[static_cast<std::size_t>(x) * d.dz + z];
                        if (prev < 0 || cur < 0)
                            continue;
                        if (cur < prev)
                        {
                            ++monotoneViolations;
                            if (monotoneViolations <= 3)
                                monoDetail += " x=" + std::to_string(x) + " z=" + std::to_string(z);
                        }
                    }
            }
            else
            {
                for (int x = 0; x < d.dx; ++x)
                    for (int z = 1; z < d.dz; ++z)
                    {
                        const int prev = columnY[static_cast<std::size_t>(x) * d.dz + (z - 1)];
                        const int cur = columnY[static_cast<std::size_t>(x) * d.dz + z];
                        if (prev < 0 || cur < 0)
                            continue;
                        if (cur < prev)
                        {
                            ++monotoneViolations;
                            if (monotoneViolations <= 3)
                                monoDetail += " x=" + std::to_string(x) + " z=" + std::to_string(z);
                        }
                    }
            }
            check(monotoneViolations == 0, d, shape,
                  "slope not monotone:" + std::to_string(monotoneViolations) + monoDetail);

            if (slopeLength > 1 && d.dy > 1)
            {
                int low = -1;
                int high = -1;
                if (alongX)
                {
                    low = columnY[0 * d.dz + 0];
                    high = columnY[static_cast<std::size_t>(d.dx - 1) * d.dz + 0];
                }
                else
                {
                    low = columnY[0];
                    high = columnY[static_cast<std::size_t>(d.dx - 1) * d.dz + (d.dz - 1)];
                }
                check(low == 0 && high == d.dy - 1, d, shape,
                      "slope ends should span 0.." + std::to_string(d.dy - 1)
                      + " got " + std::to_string(low) + ".." + std::to_string(high));
            }
            if (slopeLength == 1)
            {
                int wrongHeight = 0;
                for (const Cell &c : got)
                    if (c.y != 0)
                        ++wrongHeight;
                check(wrongHeight == 0, d, shape,
                      "slope axis length 1 must stay at y=0, wrong:" + std::to_string(wrongHeight));
            }
            break;
        }

        case ProjectionFillShape::Square:
        {
            const int shortSide = d.dx < d.dz ? d.dx : d.dz;
            const int startX = (d.dx - shortSide) / 2;
            const int startZ = (d.dz - shortSide) / 2;
            int wrong = 0;
            int wrongHeight = 0;
            std::string detail;
            for (const Cell &c : got)
            {
                if (c.y != 0)
                    ++wrongHeight;
                if (c.x < startX || c.x >= startX + shortSide || c.z < startZ
                        || c.z >= startZ + shortSide)
                {
                    ++wrong;
                    if (wrong <= 3)
                        detail += " " + cellText(c);
                }
            }
            check(wrongHeight == 0, d, shape,
                  "square must sit on y=0, wrong:" + std::to_string(wrongHeight));
            check(wrong == 0, d, shape,
                  "square cells outside inset board:" + std::to_string(wrong) + detail);
            break;
        }

        case ProjectionFillShape::Triangle:
        {
            const int shortSide = d.dx < d.dz ? d.dx : d.dz;
            const int startX = (d.dx - shortSide) / 2;
            const int startZ = (d.dz - shortSide) / 2;
            int wrong = 0;
            std::string detail;
            std::vector<int> rows(static_cast<std::size_t>(shortSide), 0);
            for (const Cell &c : got)
            {
                if (c.y != 0)
                    ++wrong;
                const int i = c.x - startX;
                const int k = c.z - startZ;
                if (i < 0 || k < 0 || i + k > shortSide - 1)
                {
                    ++wrong;
                    if (wrong <= 3)
                        detail += " " + cellText(c);
                    continue;
                }
                if (k < shortSide)
                    ++rows[static_cast<std::size_t>(k)];
            }
            check(wrong == 0, d, shape,
                  "triangle must satisfy y=0 and i+k<=s-1, violations:" + std::to_string(wrong)
                  + detail);
            // 每行应有 s - k 格，行和 = s(s+1)/2
            int rowViolations = 0;
            for (int k = 0; k < shortSide; ++k)
                if (rows[static_cast<std::size_t>(k)] != shortSide - k)
                    ++rowViolations;
            check(rowViolations == 0, d, shape,
                  "triangle row widths wrong rows:" + std::to_string(rowViolations));
            break;
        }

        case ProjectionFillShape::Circle:
        {
            const int shortSide = d.dx < d.dz ? d.dx : d.dz;
            const int startX = (d.dx - shortSide) / 2;
            const int startZ = (d.dz - shortSide) / 2;
            int wrongHeight = 0;
            int outsideFormula = 0;
            int outsideBoard = 0;
            std::string detail;
            std::string boardDetail;
            for (const Cell &c : got)
            {
                if (c.y != 0)
                    ++wrongHeight;
                if (!insideCircle(d, c.x, c.z))
                {
                    ++outsideFormula;
                    if (outsideFormula <= 3)
                        detail += " " + cellText(c);
                }
                // 圆盘必须完整落在 s x s 的内接方形板里（与方形/三角形同框）
                if (c.x < startX || c.x >= startX + shortSide || c.z < startZ
                        || c.z >= startZ + shortSide)
                {
                    ++outsideBoard;
                    if (outsideBoard <= 3)
                        boardDetail += " " + cellText(c);
                }
            }
            check(wrongHeight == 0, d, shape,
                  "circle must sit on y=0, wrong:" + std::to_string(wrongHeight));
            check(outsideFormula == 0, d, shape,
                  "circle cells outside radius formula:" + std::to_string(outsideFormula) + detail);
            check(outsideBoard == 0, d, shape,
                  "circle must stay inside the s x s inset board, violations:"
                  + std::to_string(outsideBoard) + boardDetail);
            break;
        }

        case ProjectionFillShape::Cylinder:
        {
            // 每个圆盘柱子必须从 y=0 一直长到 y=dy-1
            std::vector<int> columnHits(static_cast<std::size_t>(d.dx) * d.dz, 0);
            int wrongHeight = 0;
            std::string detail;
            for (const Cell &c : got)
            {
                const std::size_t index = static_cast<std::size_t>(c.x) * d.dz + c.z;
                ++columnHits[index];
                if (!insideCircle(d, c.x, c.z))
                {
                    ++wrongHeight;
                    if (wrongHeight <= 3)
                        detail += " " + cellText(c);
                }
            }
            for (int z = 0; z < d.dz; ++z)
                for (int x = 0; x < d.dx; ++x)
                {
                    const int hits = columnHits[static_cast<std::size_t>(x) * d.dz + z];
                    const int want = insideCircle(d, x, z) ? d.dy : 0;
                    if (hits != want)
                    {
                        ++wrongHeight;
                        if (wrongHeight <= 3)
                            detail += " column(" + std::to_string(x) + "," + std::to_string(z)
                                    + ")=" + std::to_string(hits) + " want " + std::to_string(want);
                    }
                }
            check(wrongHeight == 0, d, shape,
                  "cylinder must repeat the disc on every layer, violations:"
                  + std::to_string(wrongHeight) + detail);
            break;
        }

        case ProjectionFillShape::Frame:
        {
            int notOnEdge = 0;
            std::string detail;
            for (const Cell &c : got)
                if (!atLeastTwoAxesOnBoundary(d, c))
                {
                    ++notOnEdge;
                    if (notOnEdge <= 3)
                        detail += " " + cellText(c);
                }
            check(notOnEdge == 0, d, shape,
                  "frame cells must touch >=2 boundary axes, violations:"
                  + std::to_string(notOnEdge) + detail);
            break;
        }

        case ProjectionFillShape::Solid:
        case ProjectionFillShape::Hollow:
        case ProjectionFillShape::Count:
        default:
            break;
        }
    }

    // ------------------------------------------------------------- 形状主流程

    long long runShape(ProjectionFillShape shape, const Dim &d)
    {
        const QVector<Cell> got = ProjectionFillShapes::cells(shape, d.dx, d.dy, d.dz);
        const long long count = got.size();

        checkBoundsAndDuplicates(d, shape, got);
        checkAgainstExpectedSet(d, shape, got);
        checkCountFormula(d, shape, count);
        checkShapeSemantics(d, shape, got);

        return count;
    }

    // ------------------------------------------------------- 已知值与边界行为

    void checkKnownValues()
    {
        std::printf("\n=== known values ===\n");

        // 圆形 s=5 -> 21 格（5x1x5 的底面整好是 5 宽）
        {
            const Dim d{5, 1, 5};
            const QVector<Cell> got = ProjectionFillShapes::cells(ProjectionFillShape::Circle, d.dx, d.dy, d.dz);
            check(got.size() == 21, d, ProjectionFillShape::Circle,
                  "known circle s=5 must be 21, got " + std::to_string(got.size()));
            std::printf("  Circle  (5,1,5)  count=%lld  expected=21\n",
                        static_cast<long long>(got.size()));
        }
        // 圆形 s=4 -> 12 格
        {
            const Dim d{4, 1, 4};
            const QVector<Cell> got = ProjectionFillShapes::cells(ProjectionFillShape::Circle, d.dx, d.dy, d.dz);
            check(got.size() == 12, d, ProjectionFillShape::Circle,
                  "known circle s=4 must be 12, got " + std::to_string(got.size()));
            std::printf("  Circle  (4,1,4)  count=%lld  expected=12\n",
                        static_cast<long long>(got.size()));
        }
        // 圆盘与内接方板同框：10x7 底面 s=7、x0=1，圆仍只占 x∈[1,7]，共 37 格
        {
            const Dim d{10, 1, 7};
            const QVector<Cell> got = ProjectionFillShapes::cells(ProjectionFillShape::Circle, d.dx, d.dy, d.dz);
            check(got.size() == 37, d, ProjectionFillShape::Circle,
                  "inset circle on 10x7 board must be 37, got " + std::to_string(got.size()));
            int outsideBoard = 0;
            for (const Cell &c : got)
                if (c.x < 1 || c.x > 7 || c.z < 0 || c.z > 6)
                    ++outsideBoard;
            check(outsideBoard == 0, d, ProjectionFillShape::Circle,
                  "inset circle on 10x7 must stay in x[1,7] z[0,6], violations:"
                  + std::to_string(outsideBoard));
            std::printf("  Circle  (10,1,7) count=%lld  expected=37\n",
                        static_cast<long long>(got.size()));
        }
        // 圆柱沿用同一底面：(10,3,7) 37 * 3 = 111 格
        {
            const Dim d{10, 3, 7};
            const QVector<Cell> got = ProjectionFillShapes::cells(ProjectionFillShape::Cylinder, d.dx, d.dy, d.dz);
            check(got.size() == 111, d, ProjectionFillShape::Cylinder,
                  "inset cylinder 10x3x7 must be 37*3=111, got " + std::to_string(got.size()));
            std::printf("  Cylinder(10,3,7) count=%lld  expected=111\n",
                        static_cast<long long>(got.size()));
        }
        // 框架 3x3x3 -> 20 格
        {
            const Dim d{3, 3, 3};
            const QVector<Cell> got = ProjectionFillShapes::cells(ProjectionFillShape::Frame, d.dx, d.dy, d.dz);
            check(got.size() == 20, d, ProjectionFillShape::Frame,
                  "known frame 3x3x3 must be 20, got " + std::to_string(got.size()));
            std::printf("  Frame   (3,3,3)  count=%lld  expected=20\n",
                        static_cast<long long>(got.size()));
        }
    }

    // 全尺寸扫描：1..6 的三维组合 x 8 种形状，全部走同一套逐格/公式/语义校验，
    // 用来证明「圆盘永远落在 s x s 内接板里」不是只在几个抽查尺寸上成立。
    void checkSweep()
    {
        std::printf("\n=== sweep dx,dy,dz in 1..6 (all shapes) ===\n");
        const int limit = 6;
        for (int dx = 1; dx <= limit; ++dx)
            for (int dy = 1; dy <= limit; ++dy)
                for (int dz = 1; dz <= limit; ++dz)
                {
                    const Dim d{dx, dy, dz};
                    for (const ShapeCase &sc : kShapes)
                        runShape(sc.shape, d);
                }
        std::printf("  swept %d dims x %d shapes\n", limit * limit * limit,
                    static_cast<int>(sizeof(kShapes) / sizeof(kShapes[0])));
    }

    void checkEmptyRanges()
    {
        std::printf("\n=== empty ranges / invalid shape ===\n");

        const Dim emptyDims[] = {{0, 3, 3}, {3, 0, 3}, {3, 3, 0}, {-1, 4, 4}, {4, -2, 4}, {4, 4, -3},
                                 {0, 0, 0}};
        int bad = 0;
        std::string detail;
        for (const ShapeCase &sc : kShapes)
            for (const Dim &d : emptyDims)
            {
                const QVector<Cell> got = ProjectionFillShapes::cells(sc.shape, d.dx, d.dy, d.dz);
                if (!got.isEmpty())
                {
                    ++bad;
                    if (bad <= 5)
                        detail += " " + std::string(sc.name) + dimText(d)
                                + "=" + std::to_string(got.size());
                }
            }
        check(bad == 0, Dim{}, ProjectionFillShape::Count,
              "empty ranges must return nothing:" + std::to_string(bad) + detail);

        for (int raw : {-1, static_cast<int>(ProjectionFillShape::Count), 99})
        {
            const auto shape = static_cast<ProjectionFillShape>(raw);
            const QVector<Cell> got = ProjectionFillShapes::cells(shape, 4, 4, 4);
            check(got.isEmpty(), Dim{4, 4, 4}, shape,
                  "invalid shape raw=" + std::to_string(raw) + " must return nothing, got "
                  + std::to_string(got.size()));
        }
    }

    void checkTextsAndGroups()
    {
        std::printf("\n=== display name / description / group ===\n");

        const char *expectedNames[] = {"实心", "空心", "框架", "倾斜地板",
                                       "方形", "三角形", "圆形", "圆柱"};
        const int expectedGroups[] = {0, 0, 0, 0, 1, 1, 1, 1};

        for (int i = 0; i < static_cast<int>(sizeof(kShapes) / sizeof(kShapes[0])); ++i)
        {
            const ProjectionFillShape shape = kShapes[i].shape;
            const QString name = ProjectionFillShapes::displayName(shape);
            const QString desc = ProjectionFillShapes::description(shape);
            const int group = ProjectionFillShapes::groupOf(shape);

            check(name == QString::fromUtf8(expectedNames[i]), Dim{}, shape,
                  std::string("displayName '") + name.toUtf8().constData() + "'");
            check(!desc.isEmpty(), Dim{}, shape, "description must not be empty");
            check(group == expectedGroups[i], Dim{}, shape,
                  "groupOf=" + std::to_string(group) + " expected="
                  + std::to_string(expectedGroups[i]));

            std::printf("  %-12s name=ok desc_len=%lld group=%d\n", kShapes[i].name,
                        static_cast<long long>(desc.size()), group);
        }

        // 非法形状：名称/说明为空，分组为 -1
        check(ProjectionFillShapes::displayName(ProjectionFillShape::Count).isEmpty(), Dim{},
              ProjectionFillShape::Count, "Count displayName must be empty");
        check(ProjectionFillShapes::description(ProjectionFillShape::Count).isEmpty(), Dim{},
              ProjectionFillShape::Count, "Count description must be empty");
        check(ProjectionFillShapes::groupOf(ProjectionFillShape::Count) == -1, Dim{},
              ProjectionFillShape::Count, "Count groupOf must be -1");
    }
} // namespace

int main()
{
    std::printf("ProjectionFillShape probe\n");

    for (const Dim &d : kRequiredDims)
    {
        std::printf("\n=== dim %s ===\n", dimText(d).c_str());
        std::printf("  %-13s %8s %10s  %s\n", "shape", "count", "expected", "result");

        for (const ShapeCase &sc : kShapes)
        {
            const int failuresBefore = g_failures;
            const long long count = runShape(sc.shape, d);
            const long long want = expectedCount(sc.shape, d);
            const bool ok = g_failures == failuresBefore;

            std::printf("  %-13s %8lld %10lld  %s\n", sc.name, count, want,
                        ok ? "OK" : "FAIL");
        }
    }

    checkKnownValues();
    checkSweep();
    checkEmptyRanges();
    checkTextsAndGroups();

    std::printf("\n=== summary ===\n");
    std::printf("checks: %d   failures: %d\n", g_checks, g_failures);

    if (g_failures == 0)
    {
        std::printf("RESULT: ALL GREEN\n");
        return 0;
    }

    std::printf("RESULT: FAILED\n");
    return 1;
}
