/**
 * @file   StructureLoot.cpp
 * @brief  结构战利品：原版权重掷取实现
 * @author BlockBox Team
 * @date   2026-10-26
 */

#include "StructureLoot.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>

#include "McRandom.h"
#include "utils/JarUtils.h"

namespace StructureLoot {

namespace {

/** 中文物品名（键为去掉命名空间的 id） */
const QHash<QString, QString> &itemZh()
{
    static const QHash<QString, QString> k = {
        { QStringLiteral("air"), QStringLiteral("空气") },
        { QStringLiteral("apple"), QStringLiteral("苹果") },
        { QStringLiteral("armor_trim_smithing_template"),
          QStringLiteral("盔甲纹饰锻造模板") },
        { QStringLiteral("arrow"), QStringLiteral("箭") },
        { QStringLiteral("bamboo"), QStringLiteral("竹子") },
        { QStringLiteral("beetroot_seeds"), QStringLiteral("甜菜种子") },
        { QStringLiteral("blaze_rod"), QStringLiteral("烈焰棒") },
        { QStringLiteral("book"), QStringLiteral("书") },
        { QStringLiteral("bone"), QStringLiteral("骨头") },
        { QStringLiteral("bow"), QStringLiteral("弓") },
        { QStringLiteral("bread"), QStringLiteral("面包") },
        { QStringLiteral("bucket"), QStringLiteral("桶") },
        { QStringLiteral("buried_treasure_map"),
          QStringLiteral("海洋探险家地图") },
        { QStringLiteral("carrot"), QStringLiteral("胡萝卜") },
        { QStringLiteral("chainmail_chestplate"), QStringLiteral("锁链胸甲") },
        { QStringLiteral("clock"), QStringLiteral("时钟") },
        { QStringLiteral("coal"), QStringLiteral("煤炭") },
        { QStringLiteral("compass"), QStringLiteral("指南针") },
        { QStringLiteral("cooked_cod"), QStringLiteral("熟鳕鱼") },
        { QStringLiteral("cooked_salmon"), QStringLiteral("熟鲑鱼") },
        { QStringLiteral("copper_horse_armor"), QStringLiteral("铜马铠") },
        { QStringLiteral("crossbow"), QStringLiteral("弩") },
        { QStringLiteral("diamond"), QStringLiteral("钻石") },
        { QStringLiteral("diamond_chestplate"), QStringLiteral("钻石胸甲") },
        { QStringLiteral("diamond_hoe"), QStringLiteral("钻石锄") },
        { QStringLiteral("diamond_horse_armor"), QStringLiteral("钻石马铠") },
        { QStringLiteral("diamond_pickaxe"), QStringLiteral("钻石镐") },
        { QStringLiteral("diamond_sword"), QStringLiteral("钻石剑") },
        { QStringLiteral("dune_armor_trim_smithing_template"),
          QStringLiteral("沙丘纹饰锻造模板") },
        { QStringLiteral("emerald"), QStringLiteral("绿宝石") },
        { QStringLiteral("enchanted_book"), QStringLiteral("附魔书") },
        { QStringLiteral("enchanted_golden_apple"),
          QStringLiteral("附魔金苹果") },
        { QStringLiteral("experience_bottle"), QStringLiteral("附魔之瓶") },
        { QStringLiteral("feather"), QStringLiteral("羽毛") },
        { QStringLiteral("fermented_spider_eye"),
          QStringLiteral("发酵蛛眼") },
        { QStringLiteral("flint"), QStringLiteral("燧石") },
        { QStringLiteral("flint_and_steel"), QStringLiteral("打火石") },
        { QStringLiteral("gold_ingot"), QStringLiteral("金锭") },
        { QStringLiteral("gold_nugget"), QStringLiteral("金粒") },
        { QStringLiteral("golden_apple"), QStringLiteral("金苹果") },
        { QStringLiteral("golden_horse_armor"), QStringLiteral("金马铠") },
        { QStringLiteral("golden_pickaxe"), QStringLiteral("金镐") },
        { QStringLiteral("gunpowder"), QStringLiteral("火药") },
        { QStringLiteral("heart_of_the_sea"), QStringLiteral("海洋之心") },
        { QStringLiteral("iron_axe"), QStringLiteral("铁斧") },
        { QStringLiteral("iron_boots"), QStringLiteral("铁靴子") },
        { QStringLiteral("iron_helmet"), QStringLiteral("铁头盔") },
        { QStringLiteral("iron_hoe"), QStringLiteral("铁锄") },
        { QStringLiteral("iron_horse_armor"), QStringLiteral("铁马铠") },
        { QStringLiteral("iron_ingot"), QStringLiteral("铁锭") },
        { QStringLiteral("iron_nugget"), QStringLiteral("铁粒") },
        { QStringLiteral("iron_pickaxe"), QStringLiteral("铁镐") },
        { QStringLiteral("iron_sword"), QStringLiteral("铁剑") },
        { QStringLiteral("lapis_lazuli"), QStringLiteral("青金石") },
        { QStringLiteral("lead"), QStringLiteral("拴绳") },
        { QStringLiteral("leather"), QStringLiteral("皮革") },
        { QStringLiteral("leather_boots"), QStringLiteral("皮革靴子") },
        { QStringLiteral("leather_chestplate"), QStringLiteral("皮革胸甲") },
        { QStringLiteral("leather_helmet"), QStringLiteral("皮革帽子") },
        { QStringLiteral("leather_leggings"), QStringLiteral("皮革裤子") },
        { QStringLiteral("map"), QStringLiteral("地图") },
        { QStringLiteral("melon_seeds"), QStringLiteral("西瓜种子") },
        { QStringLiteral("moss_block"), QStringLiteral("苔藓块") },
        { QStringLiteral("music_disc_13"), QStringLiteral("音乐唱片：13") },
        { QStringLiteral("music_disc_cat"), QStringLiteral("音乐唱片：Cat") },
        { QStringLiteral("obsidian"), QStringLiteral("黑曜石") },
        { QStringLiteral("paper"), QStringLiteral("纸") },
        { QStringLiteral("poisonous_potato"), QStringLiteral("有毒马铃薯") },
        { QStringLiteral("potato"), QStringLiteral("马铃薯") },
        { QStringLiteral("potion"), QStringLiteral("药水") },
        { QStringLiteral("prismarine_crystals"), QStringLiteral("海晶碎片") },
        { QStringLiteral("pumpkin"), QStringLiteral("南瓜") },
        { QStringLiteral("pumpkin_seeds"), QStringLiteral("南瓜种子") },
        { QStringLiteral("quartz"), QStringLiteral("下界石英") },
        { QStringLiteral("redstone"), QStringLiteral("红石粉") },
        { QStringLiteral("rotten_flesh"), QStringLiteral("腐肉") },
        { QStringLiteral("sand"), QStringLiteral("沙子") },
        { QStringLiteral("saddle"), QStringLiteral("鞍") },
        { QStringLiteral("sculk"), QStringLiteral("幽匿块") },
        { QStringLiteral("sculk_catalyst"), QStringLiteral("幽匿催发体") },
        { QStringLiteral("sculk_sensor"), QStringLiteral("幽匿感测体") },
        { QStringLiteral("sculk_shrieker"), QStringLiteral("幽匿尖啸体") },
        { QStringLiteral("shears"), QStringLiteral("剪刀") },
        { QStringLiteral("skeleton_skull"), QStringLiteral("骷髅头颅") },
        { QStringLiteral("spyglass"), QStringLiteral("望远镜") },
        { QStringLiteral("stick"), QStringLiteral("木棍") },
        { QStringLiteral("string"), QStringLiteral("线") },
        { QStringLiteral("suspicious_stew"), QStringLiteral("可疑的炖菜") },
        { QStringLiteral("sweet_berries"), QStringLiteral("甜浆果") },
        { QStringLiteral("totem_of_undying"), QStringLiteral("不死图腾") },
        { QStringLiteral("tnt"), QStringLiteral("TNT") },
        { QStringLiteral("trial_key"), QStringLiteral("试炼密室钥匙") },
        { QStringLiteral("trident"), QStringLiteral("三叉戟") },
        { QStringLiteral("wheat"), QStringLiteral("小麦") },
        { QStringLiteral("wild_armor_trim_smithing_template"),
          QStringLiteral("荒野纹饰锻造模板") },
        { QStringLiteral("wooden_sword"), QStringLiteral("木剑") },
        { QStringLiteral("oak_planks"), QStringLiteral("橡木木板") },
        { QStringLiteral("oak_log"), QStringLiteral("橡木原木") },
        { QStringLiteral("chest_minecart"), QStringLiteral("运输矿车") },
        { QStringLiteral("iron_bars"), QStringLiteral("铁栏杆") },
        { QStringLiteral("activator_rail"), QStringLiteral("激活铁轨") },
        { QStringLiteral("detector_rail"), QStringLiteral("探测铁轨") },
        { QStringLiteral("powered_rail"), QStringLiteral("充能铁轨") },
        { QStringLiteral("rail"), QStringLiteral("铁轨") },
        { QStringLiteral("ladder"), QStringLiteral("梯子") },
        { QStringLiteral("torch"), QStringLiteral("火把") },
        { QStringLiteral("lantern"), QStringLiteral("灯笼") },
        { QStringLiteral("soul_torch"), QStringLiteral("灵魂火把") },
        { QStringLiteral("bone_meal"), QStringLiteral("骨粉") },
        { QStringLiteral("rabbit_hide"), QStringLiteral("兔子皮") },
        { QStringLiteral("rabbit_foot"), QStringLiteral("兔子脚") },
        { QStringLiteral("golden_carrot"), QStringLiteral("金胡萝卜") },
        { QStringLiteral("glowstone_dust"), QStringLiteral("荧石粉") },
        { QStringLiteral("nether_wart"), QStringLiteral("下界疣") },
        { QStringLiteral("blaze_powder"), QStringLiteral("烈焰粉") },
        { QStringLiteral("magma_cream"), QStringLiteral("岩浆膏") },
        { QStringLiteral("ghast_tear"), QStringLiteral("恶魂之泪") },
        { QStringLiteral("gold_block"), QStringLiteral("金块") },
        { QStringLiteral("iron_block"), QStringLiteral("铁块") },
        { QStringLiteral("diamond_block"), QStringLiteral("钻石块") },
        { QStringLiteral("emerald_block"), QStringLiteral("绿宝石块") },
        { QStringLiteral("netherite_scrap"), QStringLiteral("下界合金碎片") },
        { QStringLiteral("netherite_ingot"), QStringLiteral("下界合金锭") },
        { QStringLiteral("ancient_debris"), QStringLiteral("远古残骸") },
        { QStringLiteral("crying_obsidian"), QStringLiteral("哭泣的黑曜石") },
        { QStringLiteral("fire_charge"), QStringLiteral("火焰弹") },
        { QStringLiteral("netherrack"), QStringLiteral("下界岩") },
        { QStringLiteral("soul_sand"), QStringLiteral("灵魂沙") },
        { QStringLiteral("blaze_head"), QStringLiteral("烈焰人头颅") },
        { QStringLiteral("wither_skeleton_skull"),
          QStringLiteral("凋灵骷髅头颅") },
        { QStringLiteral("eye_armor_trim_smithing_template"),
          QStringLiteral("之眼纹饰锻造模板") },
        { QStringLiteral("coast_armor_trim_smithing_template"),
          QStringLiteral("海岸纹饰锻造模板") },
        { QStringLiteral("vex_armor_trim_smithing_template"),
          QStringLiteral("恼鬼纹饰锻造模板") },
        { QStringLiteral("resin_clump"), QStringLiteral("树脂团") },
        { QStringLiteral("echo_shard"), QStringLiteral("回响碎片") },
        { QStringLiteral("disc_fragment_5"), QStringLiteral("唱片残片：5") },
        { QStringLiteral("music_disc_otherside"),
          QStringLiteral("音乐唱片：otherside") },
        { QStringLiteral("music_disc_relic"),
          QStringLiteral("音乐唱片：Relic") },
        { QStringLiteral("goat_horn"), QStringLiteral("山羊角") },
        { QStringLiteral("amethyst_shard"), QStringLiteral("紫水晶碎片") },
        { QStringLiteral("raw_iron"), QStringLiteral("粗铁") },
        { QStringLiteral("raw_gold"), QStringLiteral("粗金") },
        { QStringLiteral("raw_copper"), QStringLiteral("粗铜") },
        { QStringLiteral("copper_ingot"), QStringLiteral("铜锭") },
        { QStringLiteral("iron_nugget"), QStringLiteral("铁粒") },
        { QStringLiteral("enchanted_golden_apple"),
          QStringLiteral("附魔金苹果") },
    };
    return k;
}

QString zhItem(const QString &itemId)
{
    QString id = itemId;
    if (id.startsWith(QLatin1String("minecraft:")))
        id = id.mid(10);
    const auto &m = itemZh();
    auto it = m.constFind(id);
    if (it != m.constEnd())
        return it.value();
    // 模板类：xxx_armor_trim_smithing_template
    if (id.endsWith(QLatin1String("_armor_trim_smithing_template")))
        return id + QStringLiteral("（纹饰模板）");
    // 音乐唱片
    if (id.startsWith(QLatin1String("music_disc_")))
        return QStringLiteral("音乐唱片：") + id.mid(11);
    return id; // 未知则显示英文 id
}

/** 结构 key → 战利品表列表（每项一个箱子） */
struct ChestPlan
{
    QString tableId; ///< minecraft:chests/xxx
    int count;       ///< 该表箱子数
};

const QHash<QString, QList<ChestPlan>> &structurePlans()
{
    static const QHash<QString, QList<ChestPlan>> k = {
        { QStringLiteral("village"),
          { { QStringLiteral("minecraft:chests/village/armorer"), 1 },
            { QStringLiteral("minecraft:chests/village/butcher"), 1 },
            { QStringLiteral("minecraft:chests/village/cartographer"), 1 },
            { QStringLiteral("minecraft:chests/village/desert_house"), 1 },
            { QStringLiteral("minecraft:chests/village/fletcher"), 1 },
            { QStringLiteral("minecraft:chests/village/mason"), 1 },
            { QStringLiteral("minecraft:chests/village/plains_house"), 1 },
            { QStringLiteral("minecraft:chests/village/savanna_house"), 1 },
            { QStringLiteral("minecraft:chests/village/shepherd"), 1 },
            { QStringLiteral("minecraft:chests/village/snowy_house"), 1 },
            { QStringLiteral("minecraft:chests/village/taiga_house"), 1 },
            { QStringLiteral("minecraft:chests/village/temple"), 1 },
            { QStringLiteral("minecraft:chests/village/toolsmith"), 1 },
            { QStringLiteral("minecraft:chests/village/weaponsmith"), 1 } } },
        { QStringLiteral("outpost"),
          { { QStringLiteral("minecraft:chests/pillager_outpost"), 2 } } },
        { QStringLiteral("monument"), {} },
        { QStringLiteral("mansion"),
          { { QStringLiteral("minecraft:chests/woodland_mansion"), 2 } } },
        { QStringLiteral("stronghold"),
          { { QStringLiteral("minecraft:chests/stronghold_library"), 2 },
            { QStringLiteral("minecraft:chests/stronghold_corridor"), 1 },
            { QStringLiteral("minecraft:chests/stronghold_crossing"), 1 },
            { QStringLiteral("minecraft:chests/stronghold_portal_room"), 1 },
            { QStringLiteral("minecraft:chests/stronghold_storage"), 1 } } },
        { QStringLiteral("desert_pyramid"),
          { { QStringLiteral("minecraft:chests/desert_pyramid"), 4 } } },
        { QStringLiteral("jungle_temple"),
          { { QStringLiteral("minecraft:chests/jungle_temple"), 2 } } },
        { QStringLiteral("swamp_hut"), {} },
        { QStringLiteral("igloo"),
          { { QStringLiteral("minecraft:chests/igloo"), 1 } } },
        { QStringLiteral("ocean_ruin"),
          { { QStringLiteral("minecraft:chests/ocean_ruin"), 1 } } },
        { QStringLiteral("shipwreck"),
          { { QStringLiteral("minecraft:chests/shipwreck_supply"), 1 },
            { QStringLiteral("minecraft:chests/shipwreck_treasure"), 1 },
            { QStringLiteral("minecraft:chests/shipwreck_map"), 1 } } },
        { QStringLiteral("ruined_portal"),
          { { QStringLiteral("minecraft:chests/ruined_portal"), 1 } } },
        { QStringLiteral("ancient_city"),
          { { QStringLiteral("minecraft:chests/ancient_city"), 2 },
            { QStringLiteral("minecraft:chests/ancient_city_ice_box"), 1 } } },
        { QStringLiteral("buried_treasure"),
          { { QStringLiteral("minecraft:chests/buried_treasure"), 1 } } },
        { QStringLiteral("mineshaft"),
          { { QStringLiteral("minecraft:chests/abandoned_mineshaft"), 2 } } },
        { QStringLiteral("trial_chambers"),
          { { QStringLiteral("minecraft:chests/trial_chambers_reward_common"), 2 },
            { QStringLiteral("minecraft:chests/trial_chambers_reward_rare"), 1 },
            { QStringLiteral("minecraft:chests/trial_chambers_reward_unique"), 1 } } },
        { QStringLiteral("trail_ruins"),
          { { QStringLiteral("minecraft:chests/trail_ruins_archaeology_rare"), 1 } } },
        { QStringLiteral("desert_well"), {} },
        { QStringLiteral("abandoned_camp"), {} },
        { QStringLiteral("fortress"),
          { { QStringLiteral("minecraft:chests/nether_bridge"), 1 } } },
        { QStringLiteral("bastion"),
          { { QStringLiteral("minecraft:chests/bastion_treasure"), 2 },
            { QStringLiteral("minecraft:chests/bastion_hoglin_stable"), 1 },
            { QStringLiteral("minecraft:chests/bastion_housing"), 1 },
            { QStringLiteral("minecraft:chests/bastion_other"), 1 } } },
        { QStringLiteral("ruined_portal_nether"),
          { { QStringLiteral("minecraft:chests/ruined_portal"), 1 } } },
        { QStringLiteral("nether_fossil"), {} },
        { QStringLiteral("end_city"),
          { { QStringLiteral("minecraft:chests/end_city_treasure"), 2 } } },
        { QStringLiteral("end_gateway"), {} },
        { QStringLiteral("end_ship"),
          { { QStringLiteral("minecraft:chests/end_city_treasure"), 1 } } },
    };
    return k;
}

/** 表 id → 本地 qrc / 文件名 */
QString tableFileName(const QString &tableId)
{
    QString path = tableId;
    if (path.startsWith(QLatin1String("minecraft:")))
        path = path.mid(10);
    if (path.startsWith(QLatin1String("chests/")))
        path = path.mid(7);
    path.replace(QLatin1Char('/'), QLatin1Char('_'));
    return path + QStringLiteral(".json");
}

QString g_clientJar;

QMutex g_tableCacheMutex;
QHash<QString, QJsonObject> g_tableCache;

bool parseTableJson(const QByteArray &data, QJsonObject *out)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    *out = doc.object();
    return true;
}

/** 读战利品表：内嵌 qrc → 客户端 JAR → 磁盘 */
QJsonObject loadTable(const QString &tableId)
{
    {
        QMutexLocker lock(&g_tableCacheMutex);
        auto it = g_tableCache.constFind(tableId);
        if (it != g_tableCache.constEnd())
            return it.value();
    }

    QJsonObject obj;
    // resources_data.qrc 里路径为 utils/WorldMap/loot_tables/xxx.json
    const QString resPath =
        QStringLiteral(":/utils/WorldMap/loot_tables/") + tableFileName(tableId);

    if (QFile::exists(resPath))
    {
        QFile f(resPath);
        if (f.open(QIODevice::ReadOnly) && parseTableJson(f.readAll(), &obj))
        {
            QMutexLocker lock(&g_tableCacheMutex);
            g_tableCache.insert(tableId, obj);
            return obj;
        }
    }

    // 客户端 JAR（1.18+ data/minecraft/loot_table/…；旧版 data/loot_tables/…）
    if (!g_clientJar.isEmpty())
    {
        const QString shortId =
            tableId.startsWith(QLatin1String("minecraft:"))
                ? tableId.mid(10)
                : tableId;
        const QStringList candidates = {
            QStringLiteral("data/minecraft/loot_table/") + shortId
                + QStringLiteral(".json"),
            QStringLiteral("data/minecraft/loot_tables/") + shortId
                + QStringLiteral(".json"),
        };
        for (const QString &entry : candidates)
        {
            QByteArray raw;
            if (JarUtils::extractFromJarToMemory(g_clientJar, entry, raw)
                && parseTableJson(raw, &obj))
            {
                QMutexLocker lock(&g_tableCacheMutex);
                g_tableCache.insert(tableId, obj);
                return obj;
            }
        }
    }

    QMutexLocker lock(&g_tableCacheMutex);
    g_tableCache.insert(tableId, QJsonObject()); // 缓存失败
    return obj;
}

// ── 数量 / 轮数 provider ──

int readIntProvider(const QJsonValue &v, McRandom::Xoroshiro &rng,
                    int def = 1)
{
    if (v.isDouble())
        return int(v.toDouble());
    if (!v.isObject())
        return def;
    const QJsonObject o = v.toObject();
    const QString type =
        o.value(QStringLiteral("type")).toString(QStringLiteral("minecraft:constant"));
    if (type.endsWith(QLatin1String("constant")))
    {
        const QJsonValue c =
            o.contains(QStringLiteral("value")) ? o.value(QStringLiteral("value"))
                                               : o.value(QStringLiteral("count"));
        if (c.isDouble())
            return int(c.toDouble());
        return def;
    }
    if (type.endsWith(QLatin1String("uniform")))
    {
        const int mn = int(o.value(QStringLiteral("min")).toDouble(1));
        const int mx = int(o.value(QStringLiteral("max")).toDouble(mn));
        return rng.nextIntBetween(mn, mx);
    }
    if (type.endsWith(QLatin1String("binomial")))
    {
        const int n = int(o.value(QStringLiteral("n")).toDouble(1));
        const double p = o.value(QStringLiteral("p")).toDouble(0.5);
        int hits = 0;
        for (int i = 0; i < n; ++i)
            if (rng.nextFloat() < float(p))
                ++hits;
        return hits;
    }
    return def;
}

// ── 条目 / 池 ──

struct RawStack
{
    QString itemId;
    int count = 1;
    bool empty = false;
    bool enchanted = false; // 附魔书 / 附魔装备
};

/** 单个条目展开进 out（返回是否产出） */
bool expandEntry(const QJsonObject &entry, McRandom::Xoroshiro &rng,
                 QList<RawStack> &out, int depth = 0);

bool expandEntriesArray(const QJsonArray &entries, McRandom::Xoroshiro &rng,
                        QList<RawStack> &out, int depth)
{
    // 加权选择：先算总权重，再按条件过滤后选
    int total = 0;
    QList<int> weights;
    weights.reserve(entries.size());
    for (const QJsonValue &ev : entries)
    {
        const QJsonObject e = ev.toObject();
        const int w = int(e.value(QStringLiteral("weight")).toDouble(1));
        weights.append(w);
        total += w;
    }
    if (total <= 0)
        return false;
    int r = rng.nextInt(total);
    for (int i = 0; i < entries.size(); ++i)
    {
        r -= weights.at(i);
        if (r >= 0)
            continue;
        return expandEntry(entries.at(i).toObject(), rng, out, depth);
    }
    if (!entries.isEmpty())
        return expandEntry(entries.last().toObject(), rng, out, depth);
    return false;
}

bool expandEntry(const QJsonObject &entry, McRandom::Xoroshiro &rng,
                 QList<RawStack> &out, int depth)
{
    if (depth > 8)
        return false;
    const QString type =
        entry.value(QStringLiteral("type")).toString(QStringLiteral("minecraft:item"));

    if (type.endsWith(QLatin1String("empty")))
    {
        RawStack s;
        s.empty = true;
        out.append(s);
        return false;
    }

    if (type.endsWith(QLatin1String("alternatives")))
    {
        const QJsonArray ch = entry.value(QStringLiteral("children")).toArray();
        for (const QJsonValue &cv : ch)
        {
            QList<RawStack> tmp;
            if (expandEntry(cv.toObject(), rng, tmp, depth + 1))
            {
                out.append(tmp);
                return true;
            }
        }
        return false;
    }

    if (type.endsWith(QLatin1String("sequence")))
    {
        bool any = false;
        const QJsonArray ch = entry.value(QStringLiteral("children")).toArray();
        for (const QJsonValue &cv : ch)
        {
            QList<RawStack> tmp;
            if (expandEntry(cv.toObject(), rng, tmp, depth + 1))
            {
                out.append(tmp);
                any = true;
            }
        }
        return any;
    }

    if (type.endsWith(QLatin1String("group")))
    {
        bool any = false;
        const QJsonArray ch = entry.value(QStringLiteral("children")).toArray();
        for (const QJsonValue &cv : ch)
        {
            QList<RawStack> tmp;
            if (expandEntry(cv.toObject(), rng, tmp, depth + 1))
            {
                out.append(tmp);
                any = true;
            }
        }
        return any;
    }

    if (type.endsWith(QLatin1String("tag")) || type.endsWith(QLatin1String("dynamic")))
        return false;

    // item
    RawStack s;
    s.itemId = entry.value(QStringLiteral("name")).toString();
    if (s.itemId.isEmpty())
        return false;

    // modifier：对象或数组
    QJsonArray modifiers;
    const QJsonValue mod = entry.value(QStringLiteral("modifier"));
    if (mod.isObject())
        modifiers.append(mod);
    else if (mod.isArray())
        modifiers = mod.toArray();

    bool discard = false;
    for (const QJsonValue &mv : modifiers)
    {
        const QJsonObject m = mv.toObject();
        const QString mtype =
            m.value(QStringLiteral("type")).toString();
        if (mtype.endsWith(QLatin1String("set_count")))
        {
            s.count = readIntProvider(m.value(QStringLiteral("count")), rng, 1);
        }
        else if (mtype.endsWith(QLatin1String("set_data")))
        {
            // 不改名，忽略 NBT
        }
        else if (mtype.endsWith(QLatin1String("enchant_randomly"))
                 || mtype.endsWith(QLatin1String("enchant_with_levels")))
        {
            s.enchanted = true;
        }
        else if (mtype.endsWith(QLatin1String("furnace_smelt")))
        {
            // 熔炼映射：粗铁→铁锭 等（展示用）
            if (s.itemId == QLatin1String("minecraft:raw_iron"))
                s.itemId = QStringLiteral("minecraft:iron_ingot");
            else if (s.itemId == QLatin1String("minecraft:raw_gold"))
                s.itemId = QStringLiteral("minecraft:gold_ingot");
            else if (s.itemId == QLatin1String("minecraft:raw_copper"))
                s.itemId = QStringLiteral("minecraft:copper_ingot");
            else if (s.itemId == QLatin1String("minecraft:porkchop"))
                s.itemId = QStringLiteral("minecraft:cooked_porkchop");
        }
        else if (mtype.endsWith(QLatin1String("filtered")))
        {
            // 探索地图等：丢弃条件很复杂，保留物品
        }
        else if (mtype.endsWith(QLatin1String("discard")))
        {
            discard = true;
        }
        else if (mtype.endsWith(QLatin1String("exploration_map")))
        {
            // 生成地图 → 显示为地图
            s.itemId = QStringLiteral("minecraft:buried_treasure_map");
        }
        else if (mtype.endsWith(QLatin1String("set_potion")))
        {
            // 药水
            s.itemId = QStringLiteral("minecraft:potion");
        }
    }
    if (discard || s.count <= 0)
        return false;

    // 展示名：附魔书
    if (s.enchanted && s.itemId == QLatin1String("minecraft:book"))
        s.itemId = QStringLiteral("minecraft:enchanted_book");

    s.count = qBound(1, s.count, 64);
    out.append(s);
    return true;
}

void rollPool(const QJsonObject &pool, McRandom::Xoroshiro &rng,
              QList<RawStack> &out)
{
    // 池条件（简化：忽略复杂 conditions，常见为无）
    int rolls = readIntProvider(pool.value(QStringLiteral("rolls")), rng, 1);
    // 池级 modifier（如 set_potion）
    QJsonArray poolMods;
    const QJsonValue pm = pool.value(QStringLiteral("modifier"));
    if (pm.isObject())
        poolMods.append(pm);
    else if (pm.isArray())
        poolMods = pm.toArray();

    const QJsonArray entries = pool.value(QStringLiteral("entries")).toArray();
    for (int i = 0; i < rolls; ++i)
    {
        QList<RawStack> got;
        expandEntriesArray(entries, rng, got, 0);
        for (RawStack &s : got)
        {
            if (s.empty)
                continue;
            for (const QJsonValue &mv : poolMods)
            {
                const QJsonObject m = mv.toObject();
                if (m.value(QStringLiteral("type")).toString()
                        .endsWith(QLatin1String("set_potion")))
                    s.itemId = QStringLiteral("minecraft:potion");
            }
            out.append(s);
        }
    }
}

QList<RawStack> generateLoot(const QJsonObject &table, quint64 seed,
                             bool useRandomSequence, quint64 worldSeed,
                             const QString &tableId)
{
    QList<RawStack> out;
    McRandom::Xoroshiro rng = useRandomSequence
        ? McRandom::create(McRandom::randomSequenceSeed(tableId, worldSeed) + seed)
        : McRandom::create(seed);

    const QJsonArray pools = table.value(QStringLiteral("pools")).toArray();
    for (const QJsonValue &pv : pools)
        rollPool(pv.toObject(), rng, out);
    return out;
}

} // namespace

void setClientJar(const QString &jarPath)
{
    if (g_clientJar == jarPath)
        return;
    g_clientJar = jarPath;
    QMutexLocker lock(&g_tableCacheMutex);
    g_tableCache.clear();
}

Result rollLoot(const QString &structKey, quint64 worldSeed, bool hasSeed,
                qint64 x, qint64 z, int mcMinor)
{
    Result result;
    const auto planIt = structurePlans().constFind(structKey);
    if (planIt == structurePlans().constEnd())
        return result;

    // 概览：所有计划表条目并集（无种子时）
    if (!hasSeed)
    {
        QHash<QString, int> score;
        for (const ChestPlan &cp : *planIt)
        {
            const QJsonObject table = loadTable(cp.tableId);
            const QJsonArray pools = table.value(QStringLiteral("pools")).toArray();
            for (const QJsonValue &pv : pools)
            {
                const QJsonArray entries =
                    pv.toObject().value(QStringLiteral("entries")).toArray();
                for (const QJsonValue &ev : entries)
                {
                    const QJsonObject e = ev.toObject();
                    const QString type =
                        e.value(QStringLiteral("type")).toString();
                    if (type.endsWith(QLatin1String("empty")))
                        continue;
                    const QString id = e.value(QStringLiteral("name")).toString();
                    if (!id.isEmpty())
                        score[id] += int(e.value(QStringLiteral("weight")).toDouble(1));
                }
            }
        }
        QList<QPair<QString, int>> items;
        for (auto it = score.constBegin(); it != score.constEnd(); ++it)
            items.append(qMakePair(it.key(), it.value()));
        std::sort(items.begin(), items.end(),
                  [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                      if (a.second != b.second)
                          return a.second > b.second;
                      return a.first < b.first;
                  });
        for (const auto &p : items)
        {
            Stack st;
            st.name = zhItem(p.first);
            st.itemId = p.first;
            st.count = 1;
            result.stacks.append(st);
        }
        return result;
    }

    // 按种子掷取
    const bool modern = mcMinor >= 18 || mcMinor == 0; // 默认按 1.18+ Xoroshiro
    Q_UNUSED(modern);
    QHash<QString, int> merged;
    QHash<QString, QString> idByZh;
    QStringList usedTables;
    int chestIndex = 0;

    for (const ChestPlan &cp : *planIt)
    {
        const QJsonObject table = loadTable(cp.tableId);
        if (table.isEmpty())
            continue;
        const bool hasSeq =
            table.contains(QStringLiteral("random_sequence"))
            && mcMinor >= 20; // 1.20.2+ 才启用 random_sequence 路径
        const QString seqId =
            table.value(QStringLiteral("random_sequence")).toString();

        for (int c = 0; c < cp.count; ++c)
        {
            // 箱子盐：结构坐标 + 箱序 + 表 id
            quint64 salt = McRandom::mixPosition(worldSeed, x, z, 64,
                                                 quint64(chestIndex + 1));
            salt ^= quint64(c) * 0x9E3779B97F4A7C15ULL;
            const QByteArray tid = (seqId.isEmpty() ? cp.tableId : seqId).toUtf8();
            for (char ch : tid)
                salt = salt * 31ULL + quint32(quint8(ch));

            const QList<RawStack> stacks =
                generateLoot(table, salt, hasSeq, worldSeed,
                             seqId.isEmpty() ? cp.tableId : seqId);
            for (const RawStack &s : stacks)
            {
                const QString zh = zhItem(s.itemId);
                merged[zh] += s.count;
                idByZh.insert(zh, s.itemId);
            }
            ++chestIndex;
            if (!usedTables.contains(cp.tableId))
                usedTables << cp.tableId;
        }
    }

    QList<Stack> stacks;
    stacks.reserve(merged.size());
    for (auto it = merged.constBegin(); it != merged.constEnd(); ++it)
    {
        Stack st;
        st.name = it.key();
        st.itemId = idByZh.value(it.key());
        st.count = it.value();
        stacks.append(st);
    }
    std::sort(stacks.begin(), stacks.end(),
              [](const Stack &a, const Stack &b) {
                  if (a.count != b.count)
                      return a.count > b.count;
                  return a.name < b.name;
              });

    result.stacks = stacks;
    result.chests = chestIndex;
    result.fromSeed = true;
    result.tableIds = usedTables.join(QLatin1Char(','));

    // 表全没读到时回退概览，避免简介页战利品区块整块消失
    if (result.stacks.isEmpty())
        return rollLoot(structKey, worldSeed, false, x, z, mcMinor);
    return result;
}

QStringList lootFor(const QString &structKey)
{
    const Result r = rollLoot(structKey, 0, false, 0, 0);
    QStringList out;
    for (const Stack &s : r.stacks)
        out << s.name;
    return out;
}

} // namespace StructureLoot
