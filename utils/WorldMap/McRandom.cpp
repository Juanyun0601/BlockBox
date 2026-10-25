/**
 * @file   McRandom.cpp
 * @brief  Minecraft 兼容随机源实现
 * @author BlockBox Team
 * @date   2026-10-26
 */

#include "McRandom.h"

#include <QString>

namespace McRandom {

namespace {

/** Stafford mix13（Xoroshiro 初始化用，与原版一致） */
quint64 mixStafford13(quint64 v)
{
    quint64 i = (v << 31) | (v >> 33); // rotateLeft 31
    i *= 0xBF58476D1CE4E5B9ULL;
    i = (i << 27) | (i >> 37);
    i *= 0x94D049BB133111EBULL;
    i ^= i >> 31;
    return i;
}

quint64 rotl(quint64 x, int k)
{
    return (x << k) | (x >> (64 - k));
}

} // namespace

Xoroshiro::Xoroshiro(quint64 seed)
{
    // 与 net.minecraft.world.level.XoroshiroRandomSource(long) 一致
    m_s0 = mixStafford13(seed);
    m_s1 = mixStafford13(seed + 0x9E3779B97F4A7C15ULL);
}

quint64 Xoroshiro::nextLong()
{
    // 标准 Xoroshiro128++（与 Minecraft XoroshiroRandomSource.nextLong 一致）
    const quint64 s0 = m_s0;
    quint64 s1 = m_s1;
    const quint64 result = rotl(s0 + s1, 17) + s0;

    s1 ^= s0;
    m_s0 = rotl(s0, 49) ^ s1 ^ (s1 << 17);
    m_s1 = rotl(s1, 17);
    return result;
}

int Xoroshiro::nextInt(int bound)
{
    if (bound <= 0)
        return 0;
    // 与 Minecraft RandomSource.nextInt 一致：nextBits(31) + 拒绝采样
    const int bits = int(nextLong() >> 33);
    if ((bound & -bound) == bound)
        return int((qint64(bound) * qint64(bits)) >> 31);
    for (;;)
    {
        const int i = int(nextLong() >> 33);
        int j = i - (i % bound) + bound;
        if (j >= 0)
            return j;
    }
}

int Xoroshiro::nextIntBetween(int lo, int hi)
{
    if (hi <= lo)
        return lo;
    return lo + nextInt(hi - lo + 1);
}

float Xoroshiro::nextFloat()
{
    return float(nextLong() >> 40) / float(1 << 24);
}

bool Xoroshiro::nextBoolean()
{
    return (nextLong() & 1) != 0;
}

Legacy::Legacy(quint64 seed)
    : m_state((seed ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1))
{
}

int Legacy::next(int bits)
{
    m_state = (m_state * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
    return int(m_state >> (48 - bits));
}

int Legacy::nextInt(int bound)
{
    if (bound <= 0)
        return 0;
    if ((bound & -bound) == bound)
        return int((qint64(bound) * qint64(next(31))) >> 31);
    for (;;)
    {
        const int bits = next(31);
        const int val = bits % bound;
        if (bits - val + (bound - 1) >= 0)
            return val;
    }
}

int Legacy::nextIntBetween(int lo, int hi)
{
    if (hi <= lo)
        return lo;
    return lo + nextInt(hi - lo + 1);
}

quint64 Legacy::nextLong()
{
    return (quint64(next(32)) << 32) + quint64(next(32));
}

Xoroshiro create(quint64 seed)
{
    return Xoroshiro(seed);
}

quint64 mixPosition(quint64 worldSeed, qint64 x, qint64 z, qint64 y,
                    quint64 salt)
{
    // 风格对齐 WorldgenRandom.seedWithSalt（多轮乘法加法）
    quint64 l = worldSeed;
    l = l * 6364136223846793005ULL + 1442695040888963407ULL;
    l += salt;
    l += quint64(x);
    l = l * 6364136223846793005ULL + 1442695040888963407ULL;
    l += quint64(z);
    l = l * 6364136223846793005ULL + 1442695040888963407ULL;
    l += quint64(y);
    l = l * 6364136223846793005ULL + 1442695040888963407ULL;
    return l;
}

quint64 randomSequenceSeed(const QString &tableId, quint64 worldSeed)
{
    // net.minecraft RandomSequence(ResourceLocation, worldSeed) 的种子派生
    quint64 l = worldSeed;
    l = l * 6364136223846793005ULL + 1442695040888963407ULL;
    // ResourceLocation.hashCode()：31 * (31 * nsHash + pathHash) + …
    // 等价展开 toString 的 Java String.hashCode
    const QByteArray utf = tableId.toUtf8();
    quint32 idHash = 0;
    for (char ch : utf)
        idHash = idHash * 31u + quint32(quint8(ch));
    l += qint64(int32_t(idHash));
    l = l * 6364136223846793005ULL + 1442695040888963407ULL;
    return l;
}

} // namespace McRandom
