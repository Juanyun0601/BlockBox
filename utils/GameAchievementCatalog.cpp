#include "GameAchievementCatalog.h"

namespace {

const QVector<GameAchievementDef> kDefinitions = {
    // ── 主线 ──
    {QStringLiteral("minecraft:story/mine_stone"), QStringLiteral("石器时代"),
     QStringLiteral("用镐挖下第一块石头"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:story/upgrade_tools"), QStringLiteral("获得升级"),
     QStringLiteral("用石头做一把更好的镐"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:story/smelt_iron"), QStringLiteral("来硬的"),
     QStringLiteral("在熔炉里炼出一块铁锭"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:story/obtain_armor"), QStringLiteral("整装上阵"),
     QStringLiteral("穿上一件铁质盔甲"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:story/iron_tools"), QStringLiteral("这不是铁镐吗？"),
     QStringLiteral("用铁锭做一把铁镐"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:story/lava_bucket"), QStringLiteral("热腾腾的"),
     QStringLiteral("把熔岩装进铁桶"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:story/mine_diamond"), QStringLiteral("钻石！"),
     QStringLiteral("挖到第一颗钻石"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:story/shiny_gear"), QStringLiteral("金光闪闪"),
     QStringLiteral("穿上全套钻石盔甲"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:story/enchant_item"), QStringLiteral("附魔师"),
     QStringLiteral("给一件物品附魔"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:story/enter_the_nether"), QStringLiteral("我们需要再深入一点"),
     QStringLiteral("建造并进入下界传送门"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:story/follow_ender_eye"), QStringLiteral("隔墙有眼"),
     QStringLiteral("跟着末影之眼找到要塞"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:story/enter_the_end"), QStringLiteral("结束了？"),
     QStringLiteral("进入末地"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:story/cure_zombie_villager"), QStringLiteral("僵尸医生"),
     QStringLiteral("治愈一个僵尸村民"), QStringLiteral("困难")},

    // ── 下界 ──
    {QStringLiteral("minecraft:nether/obtain_blaze_rod"), QStringLiteral("与火共舞"),
     QStringLiteral("从烈焰人身上取得烈焰棒"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:nether/return_to_sender"), QStringLiteral("见鬼去吧"),
     QStringLiteral("用恶魂火球击败恶魂"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:nether/find_fortress"), QStringLiteral("阴森的要塞"),
     QStringLiteral("找到下界要塞"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:nether/fast_travel"), QStringLiteral("曲速泡"),
     QStringLiteral("用下界传送门快速旅行"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:nether/obtain_ancient_debris"), QStringLiteral("深藏不露"),
     QStringLiteral("挖到远古残骸"), QStringLiteral("困难")},
    {QStringLiteral("minecraft:nether/explore_nether"), QStringLiteral("热门旅游景点"),
     QStringLiteral("探索所有下界生物群系"), QStringLiteral("困难")},
    {QStringLiteral("minecraft:nether/netherite_armor"), QStringLiteral("覆盖以远古残骸"),
     QStringLiteral("穿上一整套下界合金盔甲"), QStringLiteral("极难")},
    {QStringLiteral("minecraft:nether/all_potions"), QStringLiteral("狂乱的鸡尾酒"),
     QStringLiteral("同时拥有所有药水效果"), QStringLiteral("极难")},
    {QStringLiteral("minecraft:nether/create_full_beacon"), QStringLiteral("信标工程师"),
     QStringLiteral("建造一座完整的信标"), QStringLiteral("极难")},

    // ── 末地 ──
    {QStringLiteral("minecraft:end/kill_dragon"), QStringLiteral("解放末地"),
     QStringLiteral("在末地击败末影龙"), QStringLiteral("困难")},
    {QStringLiteral("minecraft:end/dragon_breath"), QStringLiteral("你需要来点薄荷糖"),
     QStringLiteral("用玻璃瓶收集龙息"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:end/dragon_egg"), QStringLiteral("下一世代"),
     QStringLiteral("获得末影龙蛋"), QStringLiteral("困难")},
    {QStringLiteral("minecraft:end/elytra"), QStringLiteral("天空是极限"),
     QStringLiteral("在末地城里找到鞘翅"), QStringLiteral("困难")},
    {QStringLiteral("minecraft:end/respawn_dragon"), QStringLiteral("结束了…再一次？"),
     QStringLiteral("在末地复活末影龙"), QStringLiteral("困难")},

    // ── 冒险 ──
    {QStringLiteral("minecraft:adventure/kill_a_mob"), QStringLiteral("怪物猎人"),
     QStringLiteral("杀死一只敌对生物"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:adventure/trade"), QStringLiteral("以物易物"),
     QStringLiteral("和村民完成一次交易"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:adventure/sleep_in_bed"), QStringLiteral("甜蜜的梦"),
     QStringLiteral("在床上睡一觉"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:adventure/spyglass_at_parrot"), QStringLiteral("这是鸟吗？"),
     QStringLiteral("用望远镜观察鹦鹉"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:adventure/bullseye"), QStringLiteral("正中靶心"),
     QStringLiteral("从 30 米外命中靶心"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:adventure/trim_with_any_armor_pattern"), QStringLiteral("风格重塑"),
     QStringLiteral("用锻造模板升级一件盔甲"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:adventure/totem_of_undying"), QStringLiteral("超越生死"),
     QStringLiteral("使用一次不死图腾"), QStringLiteral("困难")},
    {QStringLiteral("minecraft:adventure/sniper_duel"), QStringLiteral("狙击手对决"),
     QStringLiteral("从 50 米外击杀一只骷髅"), QStringLiteral("困难")},
    {QStringLiteral("minecraft:adventure/hero_of_the_village"), QStringLiteral("村庄英雄"),
     QStringLiteral("成功击退一次袭击"), QStringLiteral("困难")},
    {QStringLiteral("minecraft:adventure/kill_all_mobs"), QStringLiteral("资深怪物猎人"),
     QStringLiteral("杀死每一种敌对生物"), QStringLiteral("极难")},

    // ── 农牧 ──
    {QStringLiteral("minecraft:husbandry/plant_seed"), QStringLiteral("农牧业"),
     QStringLiteral("种下一颗种子"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:husbandry/breed_an_animal"), QStringLiteral("成双成对"),
     QStringLiteral("让一对动物繁殖"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:husbandry/tame_an_animal"), QStringLiteral("永远的朋友"),
     QStringLiteral("驯服一只动物"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:husbandry/fishy_business"), QStringLiteral("钓鱼佬"),
     QStringLiteral("钓到一条鱼"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:husbandry/wax_on"), QStringLiteral("涂蜡"),
     QStringLiteral("给铜块涂上蜂蜡"), QStringLiteral("简单")},
    {QStringLiteral("minecraft:husbandry/safely_harvest_honey"), QStringLiteral("与蜂共舞"),
     QStringLiteral("用营火安全地采集蜂蜜"), QStringLiteral("普通")},
    {QStringLiteral("minecraft:husbandry/break_diamond_hoe"), QStringLiteral("终极奉献"),
     QStringLiteral("用坏一把钻石锄"), QStringLiteral("困难")},
    {QStringLiteral("minecraft:husbandry/balanced_diet"), QStringLiteral("均衡饮食"),
     QStringLiteral("吃遍所有可吃的食物"), QStringLiteral("极难")},
    {QStringLiteral("minecraft:husbandry/complete_catalogue"), QStringLiteral("猫科全书"),
     QStringLiteral("驯服所有种类的猫"), QStringLiteral("极难")},
};

} // namespace

namespace GameAchievementCatalog {

const QVector<GameAchievementDef> &all()
{
    return kDefinitions;
}

const GameAchievementDef *find(const QString &idOrTitle)
{
    for (const GameAchievementDef &def : kDefinitions)
    {
        if (def.id.compare(idOrTitle, Qt::CaseInsensitive) == 0
            || def.name.compare(idOrTitle, Qt::CaseInsensitive) == 0)
        {
            return &def;
        }
    }
    return nullptr;
}

QString difficultyForId(const QString &id)
{
    const GameAchievementDef *def = find(id);
    return def ? def->difficulty : QStringLiteral("普通");
}

QColor difficultyColor(const QString &difficulty)
{
    if (difficulty == QStringLiteral("简单"))
        return QColor(0x16, 0xA3, 0x4A);
    if (difficulty == QStringLiteral("困难"))
        return QColor(0xD9, 0x77, 0x06);
    if (difficulty == QStringLiteral("极难"))
        return QColor(0xDC, 0x26, 0x26);
    return QColor(0x25, 0x63, 0xEB);
}

} // namespace GameAchievementCatalog
