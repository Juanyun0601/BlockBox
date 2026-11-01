/**
 * @file   MushroomPredictor.cpp
 * @brief  蘑菇岛巨型蘑菇位置的精确预测（1.18+）
 * @author BlockBox Team
 * @date   2026-10-29
 */

#include "MushroomPredictor.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

extern "C" {
#include "biomes.h"
}

namespace MushroomPredictor {

namespace {

constexpr quint64 k64 = 0xFFFFFFFFFFFFFFFFULL;
constexpr quint64 kGoldenRatio = 0x9E3779B97F4A7C15ULL;
constexpr quint64 kJarInitializer = 0x6A09E667F3BCC909ULL;

quint64 rotl64(quint64 x, int k)
{
    return (x << k) | (x >> (64 - k));
}

/** RandomSupport.mixStafford13：Xoroshiro 状态初始化用的 64 位混合 */
quint64 mixStafford13(quint64 v)
{
    v ^= v >> 30;
    v *= 0xBF58476D1CE4E5B9ULL;
    v ^= v >> 27;
    v *= 0x94D049BB133111EBULL;
    v ^= v >> 31;
    return v;
}

/**
 * 原版 WorldgenRandom（1.18+）：包一层 Xoroshiro128++。
 *
 * 与普通 Xoroshiro 随机源不同，WorldgenRandom 的 next(bits) 取的是每次
 * 64 位输出的**高位**，nextLong() 则是两次 next(32) 拼接 —— 特征放置
 * 的随机数序列必须严格按这个语义走，否则位置会整体偏掉。
 */
class WorldgenRandom
{
public:
    explicit WorldgenRandom(quint64 seed) { setSeed(seed); }

    void setSeed(quint64 seed)
    {
        const quint64 lo = seed ^ kJarInitializer;
        const quint64 hi = lo + kGoldenRatio;
        m_s0 = mixStafford13(lo);
        m_s1 = mixStafford13(hi);
    }

    quint64 nextLongRaw()
    {
        const quint64 s0 = m_s0;
        quint64 s1 = m_s1;
        const quint64 result = rotl64(s0 + s1, 17) + s0;
        s1 ^= s0;
        m_s0 = rotl64(s0, 49) ^ s1 ^ (s1 << 21);
        m_s1 = rotl64(s1, 28);
        return result;
    }

    /** next(bits)：取 64 位输出的高位 */
    int nextBits(int bits)
    {
        return int(nextLongRaw() >> (64 - bits));
    }

    /** RandomSource 默认 nextLong：两次 next(32) 拼接 */
    quint64 nextLong()
    {
        const quint32 hi = quint32(nextBits(32));
        const quint32 lo = quint32(nextBits(32));
        return (quint64(hi) << 32) | quint64(lo);
    }

    int nextInt(int bound)
    {
        if (bound <= 0)
            return 0;
        if ((bound & -bound) == bound)
            return int((qint64(bound) * qint64(nextBits(31))) >> 31);
        for (;;)
        {
            const int bits = nextBits(31);
            const int val = bits % bound;
            if (bits - val + (bound - 1) >= 0)
                return val;
        }
    }

    bool nextBoolean() { return nextBits(1) != 0; }

private:
    quint64 m_s0 = 0;
    quint64 m_s1 = 0;
};

bool isMushroomIsland(int biomeId)
{
    return biomeId == mushroom_fields;
}

} // namespace

int featureIndex(int mc)
{
    // 原版特征的全局排序序号（FeatureSorter 生成），会随版本新增特征插队而改变，
    // 必须逐版本用原版实例核对。已核对：1.18.2/1.19.4 = 66、
    // 1.20.6/1.21.1/1.21.3 = 68、1.21.4 = 72；其余版本暂不预测。
    if (mc >= MC_1_21_1 && mc <= MC_NEWEST)
        return mc == MC_NEWEST ? 72 : 68;
    if (mc == MC_1_20_6)
        return 68;
    if (mc >= MC_1_18 && mc <= MC_1_19)
        return 66;
    return -1;
}

bool supported(int mc)
{
    return featureIndex(mc) >= 0;
}

QVector<Mushroom> predictChunk(quint64 seed, int mc, int chunkX, int chunkZ)
{
    QVector<Mushroom> out;
    if (!supported(mc))
        return out;

    // decorationSeed：与 ChunkGenerator.applyBiomeDecoration 一致
    WorldgenRandom deco(seed);
    const quint64 i = deco.nextLong() | 1ULL;
    const quint64 j = deco.nextLong() | 1ULL;
    const qint64 bx = qint64(chunkX) * 16;
    const qint64 bz = qint64(chunkZ) * 16;
    const quint64 decorated =
        quint64(qint64(bx) * qint64(i) + qint64(bz) * qint64(j)) ^ seed;

    // setFeatureSeed + vegetal_decoration(9)
    constexpr int kStep = 9;
    WorldgenRandom rng(decorated + quint64(featureIndex(mc))
                       + quint64(10000 * kStep));

    Mushroom m;
    m.localX = rng.nextInt(16);
    m.localZ = rng.nextInt(16);
    m.kind = rng.nextBoolean() ? Red : Brown;
    int h = rng.nextInt(3) + 4;
    if (rng.nextInt(12) == 0)
        h *= 2;
    m.height = h;
    out.push_back(m);
    return out;
}

QString blockName(Kind kind)
{
    return kind == Red ? QStringLiteral("red_mushroom_block")
                       : QStringLiteral("brown_mushroom_block");
}

void overlayCaps(QVector<qint16> &top, QVector<quint8> &kind,
                 int originX, int originZ, int grid,
                 quint64 seed, int mc,
                 const QVector<int> &ids, const QVector<float> &ys)
{
    if (!supported(mc) || grid <= 0 || top.size() < grid * grid)
        return;

    const int chunkX0 = (originX >> 4) - 1;
    const int chunkZ0 = (originZ >> 4) - 1;
    const int chunkX1 = ((originX + grid - 1) >> 4) + 1;
    const int chunkZ1 = ((originZ + grid - 1) >> 4) + 1;

    // 区块按 (x, z) 递增推进：原版区块装饰时相邻区块的伞盖会先落位，后面的蘑菇
    // 若被挡住就不会生成，这个顺序能让"被挡住"的判定与原版多数情况一致
    for (int chx = chunkX0; chx <= chunkX1; ++chx)
    {
        for (int chz = chunkZ0; chz <= chunkZ1; ++chz)
        {
            const QVector<Mushroom> list = predictChunk(seed, mc, chx, chz);
            for (const Mushroom &m : list)
            {
                // 伞盖尺寸：棕 7×7（去四角）、红 5×5（去四角）
                const int radius = (m.kind == Brown) ? 3 : 2;
                const int baseX = chx * 16 + m.localX;
                const int baseZ = chz * 16 + m.localZ;

                // 基座高度取蘑菇所在格的地表
                const int gx0 = baseX - originX;
                const int gz0 = baseZ - originZ;
                if (gx0 < 0 || gz0 < 0 || gx0 >= grid || gz0 >= grid)
                    continue;
                const int seedIdx = gz0 * grid + gx0;
                if (!isMushroomIsland(ids[seedIdx]))
                    continue;

                // 蘑菇只长在水面以上的陆地：落点近似高度低于海平面时，
                // 原版会因脚印格是水而放弃这株，预览也不该多画
                if (ys[seedIdx] < float(kMinSurfaceY))
                    continue;
                const int capY =
                    int(std::lround(ys[seedIdx])) + m.height + 1;

                for (int dz = -radius; dz <= radius; ++dz)
                {
                    for (int dx = -radius; dx <= radius; ++dx)
                    {
                        if (radius == 3 && std::abs(dx) == 3
                            && std::abs(dz) == 3)
                            continue; // 棕蘑菇四角缺块
                        if (radius == 2 && std::abs(dx) == 2
                            && std::abs(dz) == 2)
                            continue; // 红蘑菇四角缺块
                        const int gx = baseX + dx - originX;
                        const int gz = baseZ + dz - originZ;
                        if (gx < 0 || gz < 0 || gx >= grid || gz >= grid)
                            continue;
                        const int idx = gz * grid + gx;
                        if (!isMushroomIsland(ids[idx]))
                            continue;
                        if (ys[idx] < float(kMinSurfaceY))
                            continue;
                        if (top[idx] != kNoCap && top[idx] >= capY)
                            continue;
                        top[idx] = qint16(capY);
                        kind[idx] = quint8(kKindBase + int(m.kind));
                    }
                }
            }
        }
    }
}

} // namespace MushroomPredictor

