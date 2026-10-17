/**
 * @file   StructureFinder.cpp
 * @brief  按种子计算结构位置 —— Cubiomes 封装
 * @author BlockBox Team
 * @date   2026-10-11
 *
 * 遍历每种结构的「区域网格」调 `getStructurePos()`：区域边长（区块数）取自
 * cubiomes 的版本配置表（如村庄 spacing=34、separation=8 → regionSize=34 /
 * chunkRange=26），不同版本的 spacing / salt 差异由 cubiomes 负责，
 * 这里只做范围与维度筛选。
 */

#include "StructureFinder.h"

#include <QStringList>
#include <cmath>

#include "biomes.h"   // enum MCVersion
#include "finders.h"  // cubiomes（自带 extern "C" 保护）

namespace StructureFinder {

namespace {
/** 单种结构最多扫的区域网格数（防极端范围把 UI 卡死） */
constexpr qint64 kMaxRegionsPerType = 4096;

struct GenCache
{
    Generator g;
    bool inited = false;
    int mc = MC_UNDEF;
    quint64 seed = 0;
    int dim = DIM_UNDEF;
};

/**
 * Generator 缓存：cubiomes 的 biome 层状态很重，不能每次查询都重建。
 * 按 (mc, seed, dim) 复用 —— 种子/维度变化时才 setup/applySeed。
 */
Generator *generatorFor(int mc, quint64 seed, int dim)
{
    static GenCache cache;
    if (!cache.inited || cache.mc != mc || cache.seed != seed)
    {
        setupGenerator(&cache.g, mc, 0);
        cache.mc = mc;
        cache.seed = seed;
        cache.dim = DIM_UNDEF;
        cache.inited = true;
    }
    if (cache.dim != dim)
    {
        applySeed(&cache.g, dim, seed);
        cache.dim = dim;
    }
    return &cache.g;
}
} // namespace

const QVector<StructureDef> &allStructures()
{
    static const QVector<StructureDef> kDefs = {
        // 主世界（envSprite = zh.minecraft.wiki 生成结构页文字旁的小图标；
        // rare = 地图缩小时仍保留显示的稀有/重要结构）
        { Village,         "village",         "村庄",       "Village",             "Bell",                 "EnvSprite_new-village.png",      false },
        { Outpost,         "outpost",         "掠夺者前哨", "Pillager Outpost",    "",                     "EnvSprite_pillager-outpost.png", false },
        { Monument,        "monument",        "海底神殿",   "Ocean Monument",      "Prismarine Shard",     "EnvSprite_monument.png",         true  },
        { Mansion,         "mansion",         "林地府邸",   "Woodland Mansion",    "Totem of Undying",     "EnvSprite_mansion.png",          true  },
        { kStrongholdType, "stronghold",      "要塞",       "Stronghold",          "",                     "EnvSprite_stronghold.png",       true  },
        { Desert_Pyramid,  "desert_pyramid",  "沙漠神殿",   "Desert Pyramid",      "",                     "EnvSprite_desert-pyramid.png",   false },
        { Jungle_Temple,   "jungle_temple",   "丛林神庙",   "Jungle Pyramid",      "",                     "EnvSprite_jungle-pyramid.png",   false },
        { Swamp_Hut,       "swamp_hut",       "沼泽小屋",   "Swamp Hut",           "",                     "EnvSprite_swamp-hut.png",        false },
        { Igloo,           "igloo",           "冰屋",       "Igloo",               "",                     "EnvSprite_igloo.png",            false },
        { Ocean_Ruin,      "ocean_ruin",      "海底废墟",   "Cold Ocean Ruins",    "",                     "EnvSprite_ocean-ruin.png",       false },
        { Shipwreck,       "shipwreck",       "沉船",       "Shipwreck",           "",                     "EnvSprite_shipwreck.png",        false },
        { Ruined_Portal,   "ruined_portal",   "废弃传送门", "Ruined Portal",       "",                     "EnvSprite_ruined-portal.png",    false },
        { Ancient_City,    "ancient_city",    "远古城市",   "Ancient City",        "",                     "EnvSprite_ancient-city.png",     true  },
        { Treasure,        "buried_treasure", "埋藏宝藏",   "Buried Treasure",     "",                     "EnvSprite_bonus-chest.png",      false },
        { Mineshaft,       "mineshaft",       "废弃矿井",   "Mineshaft",           "",                     "EnvSprite_abandoned-mineshaft.png", false },
        { Trial_Chambers,  "trial_chambers",  "试炼密室",   "Trial Chambers",      "",                     "EnvSprite_trial-chambers.png",   true  },
        { Trail_Ruins,     "trail_ruins",     "古迹废墟",   "Trail Ruins",         "",                     "EnvSprite_trail-ruins.png",      false },
        { Desert_Well,     "desert_well",     "沙漠水井",   "Desert Well",         "",                     "EnvSprite_desert-well.png",      false },
        { kUnsupportedType, "abandoned_camp", "废弃营地",   "Abandoned Camp",      "",                     "EnvSprite_abandoned-camp.png",   false },
        // 下界
        { Fortress,        "fortress",        "下界要塞",   "Nether Fortress",     "Nether Fossil",        "EnvSprite_fortress.png",         true  },
        { Bastion,         "bastion",         "猪灵堡垒",   "Bastion Remnant",     "",                     "EnvSprite_bastion-remnant.png",  true  },
        { Ruined_Portal_N, "ruined_portal_nether", "下界废弃传送门", "Ruined Portal", "",                 "EnvSprite_ruined-portal.png",    false },
        { kUnsupportedType, "nether_fossil",  "下界化石",   "Nether Fossil",       "",                     "EnvSprite_nether-fossil.png",    false },
        // 末地
        { End_City,        "end_city",        "末地城",     "End City",            "",                     "EnvSprite_end-city.png",         true  },
        { End_Gateway,     "end_gateway",     "末地折跃门", "End Gateway",         "",                     "EnvSprite_end-gateway.png",      false },
        { kUnsupportedType, "end_ship",       "末地船",     "End Ship",            "",                     "EnvSprite_end-ship.png",         false },
    };
    return kDefs;
}

int versionToInt(const QString &mcVersion)
{
    const QStringList parts = mcVersion.split(QLatin1Char('.'));
    if (parts.size() < 2)
        return MC_UNDEF;

    bool okMajor = false, okMinor = false;
    const int major = parts[0].toInt(&okMajor);
    const int minor = parts[1].toInt(&okMinor);
    if (!okMajor || !okMinor || major < 1)
        return MC_UNDEF;
    if (major > 1)
        return MC_1_21; // 更高的主版本按最新已知处理

    switch (minor)
    {
    case 0:  return MC_1_0;
    case 1:  return MC_1_1;
    case 2:  return MC_1_2;
    case 3:  return MC_1_3;
    case 4:  return MC_1_4;
    case 5:  return MC_1_5;
    case 6:  return MC_1_6;
    case 7:  return MC_1_7;
    case 8:  return MC_1_8;
    case 9:  return MC_1_9;
    case 10: return MC_1_10;
    case 11: return MC_1_11;
    case 12: return MC_1_12;
    case 13: return MC_1_13;
    case 14: return MC_1_14;
    case 15: return MC_1_15;
    case 16: return MC_1_16;
    case 17: return MC_1_17;
    case 18: return MC_1_18;
    case 19: return MC_1_19;
    case 20: return MC_1_20;
    case 21: return MC_1_21;
    default: return minor > 21 ? MC_1_21 : MC_UNDEF;
    }
}

QVector<Hit> find(quint64 seed, int mc, int dim,
                  qint64 minX, qint64 minZ, qint64 maxX, qint64 maxZ,
                  const QVector<bool> &enabled)
{
    QVector<Hit> out;
    if (mc == MC_UNDEF || maxX < minX || maxZ < minZ)
        return out;

    const QVector<StructureDef> &defs = allStructures();
    for (int idx = 0; idx < defs.size(); ++idx)
    {
        if (idx >= enabled.size() || !enabled[idx])
            continue;

        // 要塞：环形分布（每环 3/6/10…个，共 128），不走区域网格，
        // 用 cubiomes 的 StrongholdIter 迭代（nextStronghold 内部做 biome 定位）
        if (defs[idx].type == kStrongholdType)
        {
            if (dim != 0)
                continue;
            StrongholdIter sh;
            initFirstStronghold(&sh, mc, seed);
            Generator *g = generatorFor(mc, seed, 0);
            // 早停半径：查询范围的切比雪夫半径 + 环带宽度缓冲
            const qint64 maxR =
                qMax(qMax(qAbs(minX), qAbs(maxX)), qMax(qAbs(minZ), qAbs(maxZ)))
                + 1024;
            for (;;)
            {
                const int r = nextStronghold(&sh, g);
                if (sh.index == 0)
                    break; // 版本过老不生成要塞（nextStronghold 未推进）
                if (sh.pos.x >= minX && sh.pos.x <= maxX
                    && sh.pos.z >= minZ && sh.pos.z <= maxZ)
                {
                    Hit h;
                    h.x = sh.pos.x;
                    h.z = sh.pos.z;
                    h.structIdx = idx;
                    out.append(h);
                }
                if (r <= 0)
                    break; // 第 128 个已处理完
                if (qAbs(sh.nextapprox.x) > maxR && qAbs(sh.nextapprox.z) > maxR)
                    break; // 下一环整体已超出查询范围（环距单调递增）
            }
            continue;
        }

        StructureConfig cfg;
        if (!getStructureConfig(defs[idx].type, mc, &cfg))
            continue;              // 这个版本还没有该结构
        if (cfg.dim != dim)
            continue;              // 不属于当前维度

        const qint64 span = qint64(cfg.regionSize) * 16; // 区域边长（方块）
        if (span <= 0)
            continue;

        // 扫描预算分级：regionSize=1 的类型（埋藏宝藏 / 矿井 / 水井 / 折跃门）
        // 每 16 方块一个网格点，16384 方块范围就有百万级点数（纯 RNG 很快），
        // 用老的 4096 上限会把大范围查询截断成一角；大网格类型维持原上限。
        const qint64 maxScan = cfg.regionSize <= 4 ? qint64(1) << 22
                                                    : kMaxRegionsPerType;

        const int rx0 = int(std::floor(double(minX) / double(span)));
        const int rz0 = int(std::floor(double(minZ) / double(span)));
        const int rx1 = int(std::floor(double(maxX) / double(span)));
        const int rz1 = int(std::floor(double(maxZ) / double(span)));

        bool overflow = false;
        qint64 scanned = 0;
        for (int rz = rz0; rz <= rz1 && !overflow; ++rz)
        {
            for (int rx = rx0; rx <= rx1; ++rx)
            {
                if (++scanned > maxScan)
                {
                    overflow = true; // 极端范围：收工，别卡住 UI
                    break;
                }
                Pos p;
                if (!getStructurePos(defs[idx].type, mc, seed, rx, rz, &p))
                    continue;
                if (p.x < minX || p.x > maxX || p.z < minZ || p.z > maxZ)
                    continue;
                // biome 合法性：不检查的话每个网格都会冒出一个"候选"，
                // 地图上会满屏假村庄（cubiomes 文档要求调用方用这个函数过滤）
                if (!isViableStructurePos(defs[idx].type, generatorFor(mc, seed, dim),
                                          p.x, p.z, 0))
                    continue;
                Hit h;
                h.x = p.x;
                h.z = p.z;
                h.structIdx = idx;
                out.append(h);
            }
        }
    }
    return out;
}

} // namespace StructureFinder
