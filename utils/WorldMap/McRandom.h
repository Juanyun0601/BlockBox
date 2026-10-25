/**
 * @file   McRandom.h
 * @brief  Minecraft 兼容随机源（1.18+ Xoroshiro128++ / 旧版 LCG）
 * @author BlockBox Team
 * @date   2026-10-26
 *
 * 对齐 net.minecraft.world.level.XoroshiroRandomSource 与 LegacyRandomSource，
 * 供结构战利品按原版算法掷取。
 */

#ifndef MCRANDOM_H
#define MCRANDOM_H

#include <QtGlobal>

namespace McRandom {

/** Xoroshiro128++：1.18+ RandomSource.create(long) 使用的实现 */
class Xoroshiro
{
public:
    explicit Xoroshiro(quint64 seed);

    /** 下一个 64 位（低位有效位） */
    quint64 nextLong();
    /** [0, bound) —— 与 Minecraft nextInt(int) 一致 */
    int nextInt(int bound);
    /** [lo, hi] 闭区间 */
    int nextIntBetween(int lo, int hi);
    float nextFloat();
    bool nextBoolean();

private:
    quint64 m_s0 = 0;
    quint64 m_s1 = 0;
};

/** 旧版 48 位 LCG（java.util.Random / LegacyRandomSource） */
class Legacy
{
public:
    explicit Legacy(quint64 seed);

    int next(int bits);
    int nextInt(int bound);
    int nextIntBetween(int lo, int hi);
    quint64 nextLong();

private:
    quint64 m_state;
};

/** 按 Minecraft 1.18+ 语义创建随机源（新版本走 Xoroshiro） */
Xoroshiro create(quint64 seed);

/** 结构位置相关种子混合（与原版 WorldgenRandom 加盐风格一致） */
quint64 mixPosition(quint64 worldSeed, qint64 x, qint64 z, qint64 y = 64,
                    quint64 salt = 0);

/** RandomSequence：1.20.2+ 战利品表 random_sequence 字段的种子派生 */
quint64 randomSequenceSeed(const QString &tableId, quint64 worldSeed);

} // namespace McRandom

#endif // MCRANDOM_H
