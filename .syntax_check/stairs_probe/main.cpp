// 楼梯朝向探针：用真实 jar 的 blockstate/model 解析出形状，再按渲染器的变换规则
// 计算每个盒子的世界包围盒，用来核对 facing 方向是否正确。
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QtMath>
#include <cstdio>

#include "utils/Schematic/BlockShapeLoader.h"

static void rotatePoint3(float &x, float &y, float &z,
                         float ox, float oy, float oz, float angDeg, int axis)
{
    if (qFuzzyIsNull(angDeg))
        return;
    const float a = qDegreesToRadians(angDeg);
    const float c = qCos(a), s = qSin(a);
    const float dx = x - ox, dy = y - oy, dz = z - oz;
    if (axis == 0)      { y = oy + dy * c - dz * s; z = oz + dy * s + dz * c; }
    else if (axis == 2) { x = ox + dx * c - dy * s; y = oy + dx * s + dy * c; }
    else                { x = ox + dx * c - dz * s; z = oz + dx * s + dz * c; }
}

static const char *dirName(float dx, float dz)
{
    if (qAbs(dx) < 0.1f && qAbs(dz) < 0.1f) return "center";
    if (qAbs(dx) > qAbs(dz)) return dx > 0 ? "east" : "west";
    return dz > 0 ? "south" : "north";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int yawSign = (argc > 1 && QString::fromLatin1(argv[1]) == QLatin1String("minus")) ? -1 : 1;
    const QString inst = QStringLiteral(
        "D:/BlockBox/软件发布/beta5/Windows/绿色版/BlockBox-beta5-win64-portable/.minecraft/versions/26.3");

    QString err;
    BlockShapeLoader loader;
    if (!loader.load(inst, &err))
    {
        std::printf("load failed: %s\n", qPrintable(err));
        return 2;
    }

    const char *facings[] = { "north", "south", "east", "west" };
    const char *halves[] = { "bottom", "top" };
    int bad = 0;

    for (const char *half : halves)
    {
        for (const char *facing : facings)
        {
            const QString state = QStringLiteral(
                "minecraft:oak_stairs[facing=%1,half=%2,shape=straight]")
                .arg(QString::fromLatin1(facing), QString::fromLatin1(half));
            BlockShapes sh;
            if (!loader.shapeFor(state, sh) || sh.empty())
            {
                std::printf("%-52s  no shape\n", qPrintable(state));
                ++bad;
                continue;
            }

            // 逐盒计算旋转后的世界 AABB，并取「体积最小的盒子」当作台阶（step）看它偏向哪边
            float bestVol = 1e9f;
            float stepC[3] = {0, 0, 0};
            float allMin[3] = {1e9f, 1e9f, 1e9f}, allMax[3] = {-1e9f, -1e9f, -1e9f};
            for (const ShapeBox &b : sh.boxes)
            {
                float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
                for (int bit = 0; bit < 8; ++bit)
                {
                    float px = (bit & 1) ? b.to[0] : b.from[0];
                    float py = (bit & 2) ? b.to[1] : b.from[1];
                    float pz = (bit & 4) ? b.to[2] : b.from[2];
                    if (b.rotated)
                        rotatePoint3(px, py, pz, b.rotOrigin[0], b.rotOrigin[1], b.rotOrigin[2],
                                     b.rotAngle, b.rotAxis);
                    if (b.yaw)
                        rotatePoint3(px, py, pz, 0.5f, 0.5f, 0.5f, float(yawSign * b.yaw), 1);
                    mn[0] = qMin(mn[0], px); mn[1] = qMin(mn[1], py); mn[2] = qMin(mn[2], pz);
                    mx[0] = qMax(mx[0], px); mx[1] = qMax(mx[1], py); mx[2] = qMax(mx[2], pz);
                }
                for (int a = 0; a < 3; ++a)
                {
                    allMin[a] = qMin(allMin[a], mn[a]);
                    allMax[a] = qMax(allMax[a], mx[a]);
                }
                const float vol = (mx[0] - mn[0]) * (mx[1] - mn[1]) * (mx[2] - mn[2]);
                std::printf("   box yaw=%3d x=[%.2f,%.2f] y=[%.2f,%.2f] z=[%.2f,%.2f] vol=%.3f\n",
                            b.yaw, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], vol);
                if (vol < bestVol)
                {
                    bestVol = vol;
                    stepC[0] = (mn[0] + mx[0]) * 0.5f;
                    stepC[1] = (mn[1] + mx[1]) * 0.5f;
                    stepC[2] = (mn[2] + mx[2]) * 0.5f;
                }
            }

            const float dx = stepC[0] - 0.5f, dz = stepC[2] - 0.5f;
            const char *got = dirName(dx, dz);
            const float yc = (allMin[1] + allMax[1]) * 0.5f;
            const bool ok = QString::fromLatin1(got) == QString::fromLatin1(facing);
            if (!ok)
                ++bad;
            std::printf("%s %-52s step->%-6s (dx=%+.2f dz=%+.2f) yMid=%.2f %s\n",
                        ok ? "  OK  " : " FAIL ",
                        qPrintable(QStringLiteral("half=%1 facing=%2").arg(half, facing)),
                        got, dx, dz, yc, ok ? "" : "<-- 朝向不对");
        }
    }

    // 额外对照：熔炉（正面贴图在 north），facing=east 应为 y=90
    for (const char *facing : facings)
    {
        const QString state = QStringLiteral("minecraft:furnace[facing=%1,lit=false]")
                                  .arg(QString::fromLatin1(facing));
        BlockShapes sh;
        if (!loader.shapeFor(state, sh) || sh.empty())
        {
            std::printf("furnace %-8s no shape\n", facing);
            continue;
        }
        // 找出带 #front 贴图的面在旋转后的法线方向
        for (const ShapeBox &b : sh.boxes)
        {
            for (const ShapeFace &f : b.faces)
            {
                if (!f.texture.contains(QLatin1String("furnace_front")))
                    continue;
                float nx = 0, ny = 0, nz = 0;
                if (f.dir == QLatin1String("east")) nx = 1;
                else if (f.dir == QLatin1String("west")) nx = -1;
                else if (f.dir == QLatin1String("up")) ny = 1;
                else if (f.dir == QLatin1String("down")) ny = -1;
                else if (f.dir == QLatin1String("south")) nz = 1;
                else nz = -1;
                if (b.rotated)
                    rotatePoint3(nx, ny, nz, 0, 0, 0, b.rotAngle, b.rotAxis);
                if (b.yaw)
                    rotatePoint3(nx, ny, nz, 0, 0, 0, float(yawSign * b.yaw), 1);
                const char *got = dirName(nx, nz);
                const bool ok = QString::fromLatin1(got) == QString::fromLatin1(facing);
                if (!ok)
                    ++bad;
                std::printf("%s furnace facing=%-6s front->%-6s %s\n",
                            ok ? "  OK  " : " FAIL ", facing, got, ok ? "" : "<-- 正面贴图朝向不对");
            }
        }
    }

    std::printf("\n%s (yawSign=%+d)\n", bad == 0 ? "ALL OK" : "PROBLEMS FOUND", yawSign);
    return bad == 0 ? 0 : 1;
}
