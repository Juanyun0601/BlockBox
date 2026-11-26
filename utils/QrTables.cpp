/**
 * @file   QrTables.cpp
 * @brief  二维码标准表与 GF(256) 运算实现
 * @author BlockBox Team
 * @date   2026-11-25
 */
#include "QrTables.h"

namespace
{

using QrTables::EcBlockInfo;
using QrTables::kMaxVersion;
using QrTables::kMinVersion;

/** 各版本总码字数 */
constexpr int kTotal[kMaxVersion + 1] = {0,  26, 44, 70, 100, 134,
                                         172, 196, 242, 292, 346};

/** 纠错等级 L */
const EcBlockInfo kEcLow[kMaxVersion + 1] = {
    {}, {7, 1, 19, 0, 0},  {10, 1, 34, 0, 0}, {15, 1, 55, 0, 0},
    {20, 1, 80, 0, 0},  {26, 1, 108, 0, 0}, {18, 2, 68, 0, 0}, {20, 2, 78, 0, 0},
    {24, 2, 97, 0, 0},  {30, 2, 116, 0, 0}, {18, 2, 68, 2, 69},
};

/** 纠错等级 M */
const EcBlockInfo kEcMedium[kMaxVersion + 1] = {
    {}, {10, 1, 16, 0, 0}, {16, 1, 28, 0, 0}, {26, 1, 44, 0, 0},
    {18, 2, 32, 0, 0},  {24, 2, 43, 0, 0}, {16, 4, 27, 0, 0}, {18, 4, 31, 0, 0},
    {22, 2, 38, 2, 39}, {22, 3, 36, 2, 37}, {26, 4, 43, 1, 44},
};

/** GF(256) 的指数 / 对数表（本原多项式 x^8+x^4+x^3+x^2+1） */
struct GaloisTables
{
    quint8 exp[512]{};
    quint8 log[256]{};

    GaloisTables()
    {
        int x = 1;
        for (int i = 0; i < 255; ++i)
        {
            exp[i] = static_cast<quint8>(x);
            log[x] = static_cast<quint8>(i);
            x <<= 1;
            if (x & 0x100)
                x ^= 0x11D;
        }
        for (int i = 255; i < 512; ++i)
            exp[i] = exp[i - 255];
    }
};

const GaloisTables &tables()
{
    static const GaloisTables instance;
    return instance;
}

const EcBlockInfo &clampInfo(const EcBlockInfo *table, int version)
{
    if (version < kMinVersion)
        version = kMinVersion;
    if (version > kMaxVersion)
        version = kMaxVersion;
    return table[version];
}

} // namespace

namespace QrTables
{

const EcBlockInfo &ecLow(int version) { return clampInfo(kEcLow, version); }

const EcBlockInfo &ecMedium(int version) { return clampInfo(kEcMedium, version); }

const EcBlockInfo &ec(int version, EcLevel level)
{
    return level == EcLow ? ecLow(version) : ecMedium(version);
}

int totalCodewords(int version)
{
    const int index = qBound(0, version, kMaxVersion);
    return kTotal[index];
}

int dataCodewords(int version, EcLevel level)
{
    const EcBlockInfo &info = ec(version, level);
    return info.group1Blocks * info.group1Data + info.group2Blocks * info.group2Data;
}

QVector<int> alignmentPositions(int version)
{
    switch (qBound(kMinVersion, version, kMaxVersion))
    {
    case 2: return {6, 18};
    case 3: return {6, 22};
    case 4: return {6, 26};
    case 5: return {6, 30};
    case 6: return {6, 34};
    case 7: return {6, 22, 38};
    case 8: return {6, 24, 42};
    case 9: return {6, 26, 46};
    case 10: return {6, 28, 50};
    default: return {};
    }
}

quint8 gfMultiply(quint8 a, quint8 b)
{
    if (a == 0 || b == 0)
        return 0;
    const GaloisTables &g = tables();
    return g.exp[int(g.log[a]) + int(g.log[b])];
}

quint8 gfDivide(quint8 a, quint8 b)
{
    if (b == 0)
        return 0;
    if (a == 0)
        return 0;
    const GaloisTables &g = tables();
    int value = int(g.log[a]) - int(g.log[b]);
    if (value < 0)
        value += 255;
    return g.exp[value];
}

quint8 gfPower(quint8 a, int exponent)
{
    if (exponent == 0)
        return 1;
    if (a == 0)
        return 0;
    const GaloisTables &g = tables();
    int value = (int(g.log[a]) * (exponent % 255)) % 255;
    if (value < 0)
        value += 255;
    return g.exp[value];
}

} // namespace QrTables
