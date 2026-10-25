/**
 * @file   CavePredictor.cpp
 * @brief  1.18+ 噪声洞穴密度预测实现
 * @author BlockBox Team
 * @date   2026-10-25
 *
 * 密度组合（负值 = 洞穴），全部按原版数据文件逐项复刻：
 *
 *   entrances = min( 0.37 + 入口噪声 + y梯度,
 *                    粗糙度 + clamp(max(隧道噪声1, 隧道噪声2) + 厚度, -1, 1) )
 *   tunnels   = 4·洞穴层噪声² + clamp(0.27 + cheese噪声, -1, 1)
 *               + clamp(1.5 - 0.64·斜坡奶酪, 0, 0.5)
 *   地下      = min( min(tunnels, entrances), spaghetti2D + 粗糙度 )
 *               （石柱 ≥ 0.03 时取 max 回填）
 *   近地表带  = min(斜坡奶酪, 5·entrances)   —— 只有入口能穿透
 *   最终      = min(上述按斜坡奶酪分带选择的结果, noodle)
 *
 * 斜坡奶酪（地形密度）没有廉价精确算法，用 (地表 - y) / kScSlope 线性
 * 近似；分带阈值 1.5625 对应地表下约 37 格，实测对分支选择的影响不
 * 敏感。含水层屏障（把大洞隔成房间并填水）未建模，边界因此整体偏宽；
 * 洞穴水（局部含水层水位）因水位噪声场与原版对齐不足暂不输出。
 * y < -54 的洞穴空间按原版规则必为岩浆（含水层岩浆层，精确）。
 */

#include "CavePredictor.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "biomes.h"   // MC_1_18
#include "noise.h"    // cubiomes NormalNoise（DoublePerlinNoise）

namespace CavePredictor {

namespace {

// 斜坡奶酪线性近似：SC = (surfaceY - y) / 24，阈值 1.5625 ↔ 地表下 ~37 格
constexpr double kScSlope = 24.0;
// 近地表带阈值（原版 SURFACE_DENSITY_THRESHOLD）
constexpr double kSurfaceBand = 1.5625;

// ── MD5（噪声名 → 种子哈希，与 cubiomes biomenoise.c 同口径）──

struct Md5
{
    uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    uint64_t len = 0;
    unsigned char buf[64];
    size_t bl = 0;

    static uint32_t rotl(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

    void block(const unsigned char *p)
    {
        uint32_t K[64];
        for (int i = 0; i < 64; i++)
            K[i] = uint32_t(std::floor(std::fabs(std::sin(double(i + 1))) * 4294967296.0));
        static const int S[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
        uint32_t M[16];
        std::memcpy(M, p, 64);
        uint32_t A = h[0], B = h[1], C = h[2], D = h[3];
        for (int i = 0; i < 64; i++)
        {
            uint32_t F;
            int g;
            if (i < 16)      { F = (B & C) | (~B & D); g = i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D;          g = (3 * i + 5) % 16; }
            else             { F = C ^ (B | ~D);       g = (7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + rotl(F, S[i]);
        }
        h[0] += A; h[1] += B; h[2] += C; h[3] += D;
    }

    void update(const void *data, size_t n)
    {
        const unsigned char *p = static_cast<const unsigned char *>(data);
        len += n;
        while (n)
        {
            const size_t t = (64 - bl < n) ? 64 - bl : n;
            std::memcpy(buf + bl, p, t);
            bl += t; p += t; n -= t;
            if (bl == 64) { block(buf); bl = 0; }
        }
    }

    void final(unsigned char out[16])
    {
        const uint64_t bits = len * 8;
        const unsigned char pad = 0x80;
        update(&pad, 1);
        const unsigned char z = 0;
        while (bl != 56) update(&z, 1);
        unsigned char lb[8];
        for (int i = 0; i < 8; i++) lb[i] = uint8_t(bits >> (8 * i));
        update(lb, 8);
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                out[i * 4 + j] = uint8_t(h[i] >> (8 * j));
    }
};

/** 噪声名 → 两个大端 u64（与 cubiomes 已验证的常量同口径） */
void nameSeed(const char *name, uint64_t out[2])
{
    unsigned char d[16];
    Md5 m;
    m.update(name, std::strlen(name));
    m.final(d);
    out[0] = 0; out[1] = 0;
    for (int i = 0; i < 8; i++)
    {
        out[0] = (out[0] << 8) | d[i];
        out[1] = (out[1] << 8) | d[8 + i];
    }
}

// ── 噪声集合（按线程缓存）──

struct NoiseSet
{
    DoublePerlinNoise caveLayer, cheese, entrance;
    DoublePerlinNoise sp2d, sp2dElev, sp2dMod, sp2dThick;
    DoublePerlinNoise sp3d1, sp3d2, sp3dRar, sp3dThick;
    DoublePerlinNoise spRough, spRoughMod;
    DoublePerlinNoise noodle, noodleThick, noodleRa, noodleRb;
    DoublePerlinNoise pillar, pillarRare, pillarThick;
    PerlinNoise oct[64];   // 倍频程存储（最费的 cave_cheese 也只要 14 个）
    uint64_t seed = 0;     // 已初始化的世界种子
    bool inited = false;
};

/** 全部噪声的 [名称, 首倍频程, 振幅表]（逐一取自原版 worldgen 数据） */
struct NoiseDef
{
    DoublePerlinNoise NoiseSet::*field;
    const char *name;
    int firstOctave;
    const double *amps;
    int ampCount;
};

// 振幅表（数值逐一取自原版数据文件）
const double kAmp1[] = {1.0};
const double kAmpPillar[] = {1.0, 1.0};
const double kAmpEntrance[] = {0.4, 0.5, 1.0};
const double kAmpCheese[] = {0.5, 1.0, 2.0, 1.0, 2.0, 1.0, 0.0, 2.0, 0.0};

const NoiseDef kNoiseDefs[] = {
    {&NoiseSet::caveLayer,  "minecraft:cave_layer",                     -8, kAmp1, 1},
    {&NoiseSet::cheese,     "minecraft:cave_cheese",                    -8, kAmpCheese, 9},
    {&NoiseSet::entrance,   "minecraft:cave_entrance",                  -7, kAmpEntrance, 3},
    {&NoiseSet::sp2d,       "minecraft:spaghetti_2d",                   -7, kAmp1, 1},
    {&NoiseSet::sp2dElev,   "minecraft:spaghetti_2d_elevation",         -8, kAmp1, 1},
    {&NoiseSet::sp2dMod,    "minecraft:spaghetti_2d_modulator",        -11, kAmp1, 1},
    {&NoiseSet::sp2dThick,  "minecraft:spaghetti_2d_thickness",        -11, kAmp1, 1},
    {&NoiseSet::sp3d1,      "minecraft:spaghetti_3d_1",                 -7, kAmp1, 1},
    {&NoiseSet::sp3d2,      "minecraft:spaghetti_3d_2",                 -7, kAmp1, 1},
    {&NoiseSet::sp3dRar,    "minecraft:spaghetti_3d_rarity",           -11, kAmp1, 1},
    {&NoiseSet::sp3dThick,  "minecraft:spaghetti_3d_thickness",         -8, kAmp1, 1},
    {&NoiseSet::spRough,    "minecraft:spaghetti_roughness",            -5, kAmp1, 1},
    {&NoiseSet::spRoughMod, "minecraft:spaghetti_roughness_modulator",  -8, kAmp1, 1},
    {&NoiseSet::noodle,     "minecraft:noodle",                         -8, kAmp1, 1},
    {&NoiseSet::noodleThick,"minecraft:noodle_thickness",               -8, kAmp1, 1},
    {&NoiseSet::noodleRa,   "minecraft:noodle_ridge_a",                 -7, kAmp1, 1},
    {&NoiseSet::noodleRb,   "minecraft:noodle_ridge_b",                 -7, kAmp1, 1},
    {&NoiseSet::pillar,     "minecraft:pillar",                         -7, kAmpPillar, 2},
    {&NoiseSet::pillarRare, "minecraft:pillar_rareness",                -8, kAmp1, 1},
    {&NoiseSet::pillarThick,"minecraft:pillar_thickness",               -8, kAmp1, 1},
};

/** 每线程一份噪声集合，换世界种子时重新初始化 */
NoiseSet *noiseFor(uint64_t seed)
{
    static thread_local NoiseSet ns;
    if (!ns.inited || ns.seed != seed)
    {
        Xoroshiro xr;
        xSetSeed(&xr, seed);
        const uint64_t xlo = xNextLong(&xr);
        const uint64_t xhi = xNextLong(&xr);

        int used = 0;
        for (const NoiseDef &d : kNoiseDefs)
        {
            uint64_t hs[2];
            nameSeed(d.name, hs);
            Xoroshiro pxr;
            pxr.lo = xlo ^ hs[0];
            pxr.hi = xhi ^ hs[1];
            used += xDoublePerlinInit(&(ns.*d.field), &pxr, ns.oct + used,
                                      d.amps, d.firstOctave, d.ampCount, -1);
        }
        ns.seed = seed;
        ns.inited = true;
    }
    return &ns;
}

// ── 密度公式 ──

inline double clampd(double v, double lo, double hi)
{ return v < lo ? lo : v > hi ? hi : v; }

inline double yGrad(int y, int fromY, int toY, double fromV, double toV)
{
    if (y <= fromY) return fromV;
    if (y >= toY) return toV;
    return fromV + (toV - fromV) * double(y - fromY) / double(toY - fromY);
}

inline double SN(const DoublePerlinNoise *n, double x, double y, double z)
{ return sampleDoublePerlin(n, x, y, z); }

double roughnessFn(const NoiseSet &c, double x, double y, double z)
{
    const double m = SN(&c.spRoughMod, x, y, z);
    const double r = SN(&c.spRough, x, y, z);
    return (-0.05 - 0.05 * m) * (-0.4 + std::fabs(r));
}

/** weird_scaled_sampler type_1 的稀有度映射 */
double mapper1(double r)
{
    if (r < -0.75) return 0.5;
    if (r < -0.5) return 0.75;
    if (r < 0.5) return 1.0;
    return r < 0.75 ? 2.0 : 3.0;
}

/** weird_scaled_sampler type_2 的稀有度映射 */
double mapper2(double r)
{
    if (r < -0.5) return 0.75;
    if (r < 0.0) return 1.0;
    return r < 0.5 ? 1.5 : 2.0;
}

/** 入口 + 3D 隧道（原版 overworld/caves/entrances） */
double entrancesD(const NoiseSet &c, double x, double y, double z)
{
    const double t1 = 0.37 + SN(&c.entrance, x * 0.75, y * 0.5, z * 0.75)
                    + yGrad(int(std::lround(y)), -10, 30, 0.3, 0.0);
    const double rar = SN(&c.sp3dRar, x * 2.0, y * 1.0, z * 2.0);
    const double a = mapper1(rar) * SN(&c.sp3d1, x, y, z);
    const double b = mapper1(rar) * SN(&c.sp3d2, x, y, z);
    const double thick = -0.0765 - 0.0115 * SN(&c.sp3dThick, x, y, z);
    const double t2 = roughnessFn(c, x, y, z)
                    + clampd(std::fmax(a, b) + thick, -1.0, 1.0);
    return std::fmin(t1, t2);
}

/** 2D 隧道（原版 overworld/caves/spaghetti_2d） */
double spag2dD(const NoiseSet &c, double x, double y, double z)
{
    const double mod = -0.95 - 0.35 * SN(&c.sp2dThick, x * 2.0, y * 1.0, z * 2.0);
    const double rar = SN(&c.sp2dMod, x * 2.0, y * 1.0, z * 2.0);
    const double b1 = mapper2(rar) * SN(&c.sp2d, x, y, z) + 0.083 * mod;
    const double elev = 8.0 * SN(&c.sp2dElev, x, 0.0, z);   // y_scale 0
    const double grad = yGrad(int(std::lround(y)), -64, 320, 8.0, -40.0);
    const double b2v = std::fabs(elev + grad) + mod;
    const double b2 = b2v * b2v * b2v;
    return clampd(std::fmax(b1, b2), -1.0, 1.0);
}

/** 细洞（原版 overworld/caves/noodle；只在 y∈[-60,320) 生效） */
double noodleD(const NoiseSet &c, double x, double y, double z)
{
    const int yi = int(std::lround(y));
    if (yi < -60 || yi >= 320)
        return 64.0;   // 大正值 = 永不掏空
    const double n = SN(&c.noodle, x, y, z);
    if (n < 0.0)
        return 64.0;
    const double thick = -0.075 - 0.025 * SN(&c.noodleThick, x, y, z);
    const double s = 2.6666666666666665;
    const double ra = std::fabs(SN(&c.noodleRa, x * s, y * s, z * s));
    const double rb = std::fabs(SN(&c.noodleRb, x * s, y * s, z * s));
    return thick + 1.5 * std::fmax(ra, rb);
}

/** 深板岩柱（原版 overworld/caves/pillars，把被掏空的位置顶回实心） */
double pillarsD(const NoiseSet &c, double x, double y, double z)
{
    const double p = 2.0 * SN(&c.pillar, x * 25.0, y * 0.3, z * 25.0)
                   + (-1.0 - SN(&c.pillarRare, x, y, z));
    const double t = 0.55 + 0.55 * SN(&c.pillarThick, x, y, z);
    const double t3 = t * t * t;
    return p * t3;
}

/** cheese 大洞 + 洞穴分层项（原版 underground 里的 tunnels 组成） */
double tunnelsD(const NoiseSet &c, double x, double y, double z, double sc)
{
    const double layer = SN(&c.caveLayer, x * 8.0, y * 8.0, z * 8.0);
    const double ch = SN(&c.cheese, x * 0.6666666666666666,
                         y * 0.6666666666666666, z * 0.6666666666666666);
    const double tA = clampd(0.27 + ch, -1.0, 1.0)
                    + clampd(1.5 - 0.64 * sc, 0.0, 0.5);
    return 4.0 * layer * layer + tA;
}

/** 最终洞穴密度：< 0 = 掏空 */
double caveDensity(const NoiseSet &c, double x, double y, double z, double sc)
{
    double d;
    if (sc <= kSurfaceBand)
    {
        // 近地表带：只有入口（放大 5 倍）能穿透地表
        d = std::fmin(sc, 5.0 * entrancesD(c, x, y, z));
    }
    else
    {
        const double u = std::fmin(
            std::fmin(tunnelsD(c, x, y, z, sc), entrancesD(c, x, y, z)),
            spag2dD(c, x, y, z) + roughnessFn(c, x, y, z));
        const double pil = pillarsD(c, x, y, z);
        d = (pil >= 0.03) ? std::fmax(u, pil) : u;
    }
    return std::fmin(d, noodleD(c, x, y, z));
}

} // namespace

bool supported(int mc)
{
    return mc >= MC_1_18;
}

Cell sampleAt(quint64 seed, int x, int y, int z, double surfaceY)
{
    const NoiseSet &c = *noiseFor(uint64_t(seed));
    const double sc = (surfaceY - double(y)) / kScSlope;
    if (caveDensity(c, double(x), double(y), double(z), sc) >= 0.0)
        return Cell::Solid;
    // 原版含水层规则：世界底部 -54 以下的洞穴空间必为岩浆
    // （fluidPicker 先于含水层网格判定，精确）
    if (y < -54)
        return Cell::Lava;
    return Cell::Cave;
}

} // namespace CavePredictor
