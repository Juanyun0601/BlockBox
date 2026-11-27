# -*- coding: utf-8 -*-
"""一次性数据整理脚本：修正/扩充 command_database.json 与 bedrock_command_database.json。

- Java 库剔除基岩版专属指令，给旧版移除的指令打 removedIn，给插件指令打 requiresPlugin
- 补 waypoint / test 两条新指令
- 扩写指令中文别名
- 修正物品/效果/附魔中文名，补全新物品/实体/附魔/句子预设
- 基岩库补上从 Java 库挪过来的 5 条指令
"""

import json
import os

BASE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.abspath(os.path.join(BASE, "..", "resources"))

JAVA = os.path.join(RES, "command_database.json")
BEDROCK = os.path.join(RES, "bedrock_command_database.json")

# ---------------------------------------------------------------- 指令：删/标记
# 这些是基岩版（含教育版）专属指令，Java 库里的同名条目会翻出游戏不认识的指令
JAVA_REMOVE = {
    "camerashake", "fog", "gametest", "hud", "inputpermission", "playanimation",
    "structure", "tickingarea", "toggleanimation", "camera", "ability", "agent",
    "alwaysday", "daylock", "dialogue", "event", "mobevent", "music", "script",
    "scriptevent", "setmaxplayers", "connect", "lesson", "code", "codebuilder",
    "rotate", "placefeature", "placejigsaw", "placenstructure", "placetemplate",
}

# 挪到基岩库的指令（Java 库删掉，基岩库补上）
BEDROCK_MOVE = ["placefeature", "placejigsaw", "placenstructure", "placetemplate", "rotate"]

# 已经在 1.13 被移除的旧指令：旧版本还能用，新版本应当提示不可用
JAVA_REMOVED_IN = {
    "blockdata": "1.13",
    "entitydata": "1.13",
    "testfor": "1.13",
    "testforblock": "1.13",
    "testforblocks": "1.13",
    "replaceitem": "1.13",
    "toggledownfall": "1.13",
    "achievement": "1.13",
    "stats": "1.13",
}

# 需要服务器插件（Essentials 之类）才有意义的指令
JAVA_PLUGIN = {
    "spawn", "home", "sethome", "tpa", "tpaccept", "tpahere", "tpdeny", "warp",
    "setwarp", "kit", "fly", "god", "heal", "feed", "repair", "hat", "back",
    "seen", "suicide", "balance", "pay", "eco", "nick", "mail",
}

# ---------------------------------------------------------------- 指令：别名扩充
ALIAS_ADD = {
    "help": ["怎么用", "指令帮助"],
    "list": ["玩家列表"],
    "me": ["动作"],
    "msg": ["发消息", "密语"],
    "tell": ["告诉"],
    "say": ["说话", "喊话", "全服广播"],
    "tellraw": ["原始消息"],
    "title": ["显示标题", "大标题"],
    "kill": ["消灭", "搞死"],
    "clear": ["清空背包", "清理背包", "清背包", "丢弃物品"],
    "give": ["给", "发放", "给物品"],
    "tp": ["瞬移", "移动到"],
    "teleport": ["瞬移"],
    "effect": ["添加效果", "药效"],
    "gamemode": ["模式", "改模式"],
    "time": ["调时间", "改时间", "时间设置"],
    "weather": ["改天气", "换天气"],
    "difficulty": ["改难度", "设置难度"],
    "gamerule": ["改规则"],
    "enchant": ["打附魔", "上附魔"],
    "xp": ["给经验", "加经验"],
    "damage": ["造成伤害", "扣血"],
    "summon": ["生成", "刷怪", "生成实体"],
    "setblock": ["放方块", "放一个方块"],
    "fill": ["填充方块", "填满"],
    "clone": ["克隆", "复制建筑"],
    "particle": ["特效"],
    "playsound": ["放音效"],
    "locate": ["找结构", "寻找结构"],
    "locatebiome": ["找群系"],
    "seed": ["看种子"],
    "spawnpoint": ["设置重生点"],
    "setworldspawn": ["设置世界出生点"],
    "scoreboard": ["积分板"],
    "op": ["给管理员", "授权"],
    "deop": ["撤管理员"],
    "kick": ["踢"],
    "ban": ["封号"],
    "pardon": ["解封玩家"],
    "whitelist": ["白名单管理"],
    "save-all": ["保存"],
    "reload": ["重新加载"],
    "stop": ["关服"],
    "spectate": ["观战", "进入旁观"],
    "transfer": ["转服"],
    "advancement": ["成就管理"],
    "bossbar": ["血条"],
    "data": ["NBT"],
    "recipe": ["解锁配方"],
    "loot": ["掉落物"],
    "item": ["物品操作"],
    "publish": ["开房", "开放局域网"],
    "ride": ["坐骑"],
    "tag": ["标签管理"],
    "team": ["队伍管理"],
    "defaultgamemode": ["默认模式"],
    "forceload": ["区块加载"],
    "datapack": ["数据包"],
    "worldborder": ["边界"],
    "spreadplayers": ["随机分散"],
    "place": ["放置结构"],
    "random": ["随机数"],
    "return": ["返回值"],
    "schedule": ["定时任务"],
    "function": ["函数"],
    "execute": ["执行命令"],
    "jfr": ["性能分析"],
    "perf": ["性能"],
    "setidletimeout": ["挂机踢出"],
    "banlist": ["封禁列表"],
    "save-on": ["自动保存"],
    "save-off": ["自动保存"],
}

# 新增指令（Java 1.21.x）
NEW_COMMANDS = [
    {
        "name": "waypoint",
        "chinese": ["路径点", "传送点", "定位点", "路径点管理"],
        "minVersion": "1.21.6",
        "opLevel": 2,
        "serverOnly": False,
        "syntax": "/waypoint <list|save|modify> [名称]",
        "params": [
            {"name": "操作", "type": "enum", "required": True,
             "candidates": ["list", "save", "modify"]},
            {"name": "名称", "type": "string", "required": False},
        ],
        "description": "管理路径点，可在屏幕上显示带编号的方位点",
    },
    {
        "name": "test",
        "chinese": ["测试", "运行测试", "测试指令"],
        "minVersion": "1.21.5",
        "opLevel": 2,
        "serverOnly": False,
        "syntax": "/test <run|runthese|runfailed|clearall> [测试名]",
        "params": [
            {"name": "操作", "type": "enum", "required": True,
             "candidates": ["run", "runthese", "runfailed", "clearall"]},
            {"name": "测试名", "type": "string", "required": False},
        ],
        "description": "运行或清理数据包里的测试用例",
    },
]

# ---------------------------------------------------------------- 物品：修正
ITEM_FIX = {
    "clock": ["时钟"],
    "azure_bluet": ["蓝花美耳草"],
    "allium": ["绒球葱"],
    "pitcher_pod": ["猪笼草荚"],
    "pitcher_plant": ["猪笼草"],
    "prismarine_crystals": ["海晶砂粒"],
    "crying_obsidian": ["哭泣的黑曜石"],
    "brick": ["红砖"],
    "target": ["标靶"],
}

ITEM_REMOVE = {"bottle_o_enchanting", "slimeball"}

# ---------------------------------------------------------------- 物品：新增
_WOOL = [
    ("white_wool", "白色羊毛"), ("orange_wool", "橙色羊毛"),
    ("magenta_wool", "品红色羊毛"), ("light_blue_wool", "淡蓝色羊毛"),
    ("yellow_wool", "黄色羊毛"), ("lime_wool", "黄绿色羊毛"),
    ("pink_wool", "粉红色羊毛"), ("gray_wool", "灰色羊毛"),
    ("light_gray_wool", "淡灰色羊毛"), ("cyan_wool", "青色羊毛"),
    ("purple_wool", "紫色羊毛"), ("blue_wool", "蓝色羊毛"),
    ("brown_wool", "棕色羊毛"), ("green_wool", "绿色羊毛"),
    ("red_wool", "红色羊毛"), ("black_wool", "黑色羊毛"),
]

NEW_ITEMS = [
    ("diamond_block", ["钻石块"]),
    ("iron_block", ["铁块"]),
    ("gold_block", ["金块"]),
    ("emerald_block", ["绿宝石块"]),
    ("lapis_block", ["青金石块"]),
    ("netherite_block", ["下界合金块"]),
    ("copper_block", ["铜块"]),
    ("quartz_block", ["石英块"]),
    ("bricks", ["砖块"]),
    ("command_block", ["命令方块"]),
    ("chain_command_block", ["连锁命令方块"]),
    ("repeating_command_block", ["循环命令方块"]),
    ("structure_block", ["结构方块"]),
    ("jigsaw", ["拼图方块"]),
    ("barrier", ["屏障", "空气墙"]),
    ("light", ["光源方块", "光源"]),
    ("bedrock", ["基岩"]),
    ("crafting_table", ["工作台"]),
    ("furnace", ["熔炉"]),
    ("blast_furnace", ["高炉"]),
    ("smoker", ["烟熏炉"]),
    ("chest", ["箱子"]),
    ("trapped_chest", ["陷阱箱"]),
    ("barrel", ["木桶"]),
    ("bookshelf", ["书架"]),
    ("chiseled_bookshelf", ["雕纹书架"]),
    ("cartography_table", ["制图台"]),
    ("fletching_table", ["制箭台"]),
    ("smithing_table", ["锻造台"]),
    ("loom", ["织布机"]),
    ("stonecutter", ["切石机"]),
    ("grindstone", ["砂轮"]),
    ("composter", ["堆肥桶"]),
    ("cauldron", ["炼药锅"]),
    ("brewing_stand", ["酿造台"]),
    ("beacon", ["信标", "信标方块"]),
    ("conduit", ["潮涌核心"]),
    ("spawner", ["刷怪笼"]),
    ("end_portal_frame", ["末地传送门框架"]),
    ("respawn_anchor", ["重生锚"]),
    ("lodestone", ["磁石"]),
    ("campfire", ["营火"]),
    ("soul_campfire", ["灵魂营火"]),
    ("jukebox", ["唱片机"]),
    ("rail", ["铁轨", "轨道"]),
    ("powered_rail", ["动力铁轨"]),
    ("detector_rail", ["探测铁轨"]),
    ("activator_rail", ["激活铁轨"]),
    ("minecart", ["矿车"]),
    ("sponge", ["海绵"]),
    ("wet_sponge", ["湿海绵"]),
    ("cobweb", ["蜘蛛网"]),
    ("dead_bush", ["枯萎的灌木"]),
    ("short_grass", ["矮草丛", "草丛"]),
    ("fern", ["蕨"]),
    ("brown_mushroom", ["棕色蘑菇"]),
    ("red_mushroom", ["红色蘑菇"]),
    ("pumpkin", ["南瓜"]),
    ("carved_pumpkin", ["雕刻过的南瓜"]),
    ("jack_o_lantern", ["南瓜灯"]),
    ("melon", ["西瓜"]),
    ("nether_wart_block", ["下界疣块"]),
    ("warped_wart_block", ["诡异疣块"]),
    ("magma_block", ["岩浆块"]),
    ("nether_bricks", ["下界砖块"]),
    ("red_nether_bricks", ["红色下界砖块"]),
    ("end_stone_bricks", ["末地石砖"]),
    ("purpur_block", ["紫珀块"]),
    ("purpur_pillar", ["紫珀柱"]),
    ("prismarine", ["海晶石"]),
    ("prismarine_bricks", ["海晶石砖"]),
    ("dark_prismarine", ["暗海晶石"]),
    ("amethyst_block", ["紫水晶块"]),
    ("budding_amethyst", ["紫水晶芽床"]),
    ("bed", ["床"]),
    ("red_bed", ["红色床"]),
    ("sculk", ["幽匿块"]),
    ("sculk_catalyst", ["幽匿催发体"]),
    ("sculk_sensor", ["幽匿感测体"]),
    ("calibrated_sculk_sensor", ["校准幽匿感测体"]),
    ("sculk_shrieker", ["幽匿尖啸体"]),
    ("sculk_vein", ["幽匿脉络"]),
    ("mud_bricks", ["泥砖"]),
    ("packed_mud", ["泥坯"]),
    ("moss_block", ["苔藓块"]),
    ("azalea", ["杜鹃花丛"]),
    ("flowering_azalea", ["盛开的杜鹃花丛"]),
    ("hanging_roots", ["垂根"]),
    ("mangrove_roots", ["红树根"]),
    ("big_dripleaf", ["大型垂滴叶"]),
    ("small_dripleaf", ["小型垂滴叶"]),
    ("spore_blossom", ["孢子花"]),
    ("oak_stairs", ["橡木楼梯"]),
    ("spruce_stairs", ["云杉木楼梯"]),
    ("stone_stairs", ["石头楼梯"]),
    ("cobblestone_stairs", ["圆石楼梯"]),
    ("stone_brick_stairs", ["石砖楼梯"]),
    ("oak_slab", ["橡木台阶"]),
    ("stone_slab", ["石头台阶"]),
    ("smooth_stone_slab", ["平滑石头台阶"]),
    ("oak_fence", ["橡木栅栏"]),
    ("oak_fence_gate", ["橡木栅栏门"]),
    ("oak_door", ["橡木门"]),
    ("oak_trapdoor", ["橡木活板门"]),
    ("ladder", ["梯子"]),
    ("scaffolding", ["脚手架"]),
    ("oak_sign", ["橡木告示牌", "告示牌"]),
    ("lead", ["拴绳", "牵引绳"]),
    ("totem_of_undying", ["不死图腾"]),
    ("blaze_powder", ["烈焰粉"]),
    ("magma_cream", ["岩浆膏"]),
    ("slime_ball", ["黏液球", "粘液球"]),
    ("rabbit_foot", ["兔子脚"]),
    ("rabbit_hide", ["兔子皮"]),
    ("dragon_breath", ["龙息"]),
    ("shulker_shell", ["潜影壳"]),
    ("glistering_melon_slice", ["闪烁的西瓜片"]),
    ("firework_star", ["烟火之星"]),
    ("mace", ["重锤"]),
    ("wind_charge", ["风弹"]),
    ("breeze_rod", ["旋风棒"]),
    ("heavy_core", ["沉重核心"]),
    ("armadillo_scute", ["犰狳鳞甲"]),
    ("wolf_armor", ["狼铠"]),
    ("ominous_bottle", ["不祥之瓶"]),
    ("trial_key", ["试炼钥匙"]),
    ("ominous_trial_key", ["不祥试炼钥匙"]),
] + [(en, [cn]) for en, cn in _WOOL]

# ---------------------------------------------------------------- 实体 / 效果 / 附魔
ENTITY_FIX = {
    "boat": ["船", "木船"],
}

NEW_ENTITIES = [
    ("creaking", ["嘎枝"]),
    ("strider", ["炽足兽"]),
    ("giant", ["巨人"]),
]

EFFECT_FIX = {
    "wind_charged": ["蓄风"],
    "weaving": ["盘丝"],
    "infested": ["寄生"],
    "raid_omen": ["袭击之兆"],
}

EFFECT_REMOVE = {"weeping"}

ENCHANT_FIX = {
    "sweeping": ("sweeping_edge", ["横扫之刃"]),
}

NEW_ENCHANTS = [
    ("binding_curse", ["绑定诅咒"]),
    ("vanishing_curse", ["消失诅咒"]),
]

# ---------------------------------------------------------------- 句子预设：新增
NEW_PRESETS = [
    ("clear-inventory", "物品", "清空自己的背包", "/clear @s", "清空自己背包里的所有物品"),
    ("clear-all-players", "物品", "清空所有玩家的背包", "/clear @a", "清空所有玩家背包"),
    ("give-diamond-block", "物品", "给自己一个钻石块", "/give @s diamond_block 1", "给予自己一个钻石块"),
    ("give-command-block", "物品", "给自己一个命令方块", "/give @s command_block 1", "给予自己一个命令方块"),
    ("give-beacon", "物品", "给自己一个信标", "/give @s beacon 1", "给予自己一个信标"),
    ("give-mace", "物品", "给自己一把重锤", "/give @s mace 1", "给予自己一把重锤"),
    ("kill-self", "快捷操作", "自杀", "/kill @s", "直接结束自己"),
    ("kill-all-monsters", "快捷操作", "清除所有敌对生物", "/kill @e[type=!player]", "清掉除玩家以外的所有实体，慎用"),
    ("heal-self", "快捷操作", "把血回满", "/effect give @s instant_health 1 10", "瞬间治疗 10 级，回满血量"),
    ("feed-self", "快捷操作", "去掉饥饿", "/effect give @s saturation 1 10", "瞬间回满饥饿值"),
    ("enchant-fortune-3", "附魔", "给手中物品附魔时运 III", "/enchant @s fortune 3", "时运 3 级"),
    ("enchant-silk-touch", "附魔", "给手中物品附魔精准采集", "/enchant @s silk_touch 1", "精准采集 1 级"),
    ("time-query", "时间天气", "查询当前时间", "/time query daytime", "查看当前世界时间"),

    ("gamerule-keep-inventory", "游戏规则", "死亡不掉落", "/gamerule keepInventory true", "死亡后保留背包物品"),
    ("gamerule-no-mob", "游戏规则", "禁止自然刷怪", "/gamerule doMobSpawning false", "关闭自然刷怪"),
    ("gamerule-weather-cycle", "游戏规则", "关闭天气变化", "/gamerule doWeatherCycle false", "世界天气不再自动变化"),
    ("gamerule-daylight", "游戏规则", "关闭昼夜循环", "/gamerule doDaylightCycle false", "时间停在当前时刻"),
    ("gamerule-mob-griefing", "游戏规则", "禁止生物破坏方块", "/gamerule mobGriefing false", "苦力怕、末影人不再拆家"),
    ("spawnpoint-self", "传送", "把重生点设在这里", "/spawnpoint @s", "把当前脚下设为重生点"),
    ("tp-all-to-me", "传送", "把所有玩家传送到我身边", "/tp @a @s", "把全部玩家拉过来"),
    ("tp-up-100", "传送", "向上传送 100 格", "/tp @s ~ ~100 ~", "直上 100 格"),
    ("summon-warden", "召唤生物", "在面前召唤监守者", "/summon warden ~ ~1 ~", "召唤一只监守者"),
    ("summon-allay", "召唤生物", "在面前召唤悦灵", "/summon allay ~ ~1 ~", "召唤一只悦灵"),
    ("summon-breeze", "召唤生物", "在面前召唤旋风人", "/summon breeze ~ ~1 ~", "召唤一只旋风人"),
    ("effect-night-vision-10m", "药水效果", "给自己 10 分钟夜视", "/effect give @s night_vision 600 0", "夜视 10 分钟"),
    ("effect-clear-self", "药水效果", "清除自己身上的效果", "/effect clear @s", "清掉自己的所有药水效果"),
    ("effect-speed-1m", "药水效果", "给自己 1 分钟速度 III", "/effect give @s speed 60 2", "速度 3 级 60 秒"),
    ("locate-village", "快捷操作", "寻找最近的村庄", "/locate structure #village", "定位最近的村庄坐标"),
    ("locate-stronghold", "快捷操作", "寻找最近的要塞", "/locate structure stronghold", "定位最近的要塞坐标"),
    ("seed", "快捷操作", "查看世界种子", "/seed", "显示当前世界种子"),
    ("play-sound-levelup", "快捷操作", "播放升级音效", "/playsound entity.player.levelup master @s", "给自己播一次升级音效"),
    ("title-hello", "快捷操作", "屏幕中央显示标题", "/title @a title {\"text\":\"你好\"}", "给所有玩家显示大标题"),
    ("benchmark-tps", "服务器管理", "查看服务器 TPS", "/tick query", "查询服务器运行速度"),
]


def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def dump(path, data):
    # 仓库里的 JSON 是 CRLF，保持一致，避免整文件换行变化淹没真正的数据改动
    text = json.dumps(data, ensure_ascii=False, indent=1).replace("\n", "\r\n") + "\r\n"
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)


def merge_alias(entry, extras):
    for alias in extras:
        if alias and alias not in entry["chinese"]:
            entry["chinese"].append(alias)


def main():
    data = load(JAVA)
    stats = []

    # ---- 指令 ----
    bedrock_cmds = {c["name"]: c for c in load(BEDROCK)["commands"]}
    moved = []
    kept = []
    for cmd in data["commands"]:
        name = cmd["name"]
        if name in JAVA_REMOVE:
            if name in BEDROCK_MOVE:
                moved.append(cmd)
            continue
        if name in JAVA_REMOVED_IN:
            # 插到 serverOnly 后面，保持字段顺序稳定
            new_cmd = {}
            for key, value in cmd.items():
                new_cmd[key] = value
                if key == "serverOnly":
                    new_cmd["removedIn"] = JAVA_REMOVED_IN[name]
            cmd = new_cmd
        if name in JAVA_PLUGIN:
            new_cmd = {}
            for key, value in cmd.items():
                new_cmd[key] = value
                if key == "serverOnly":
                    new_cmd["requiresPlugin"] = "Essentials"
            cmd = new_cmd
        if name in ALIAS_ADD:
            merge_alias(cmd, ALIAS_ADD[name])
        kept.append(cmd)

    existing = {c["name"] for c in kept}
    for cmd in NEW_COMMANDS:
        if cmd["name"] not in existing:
            kept.append(cmd)
            stats.append("新增指令 %s" % cmd["name"])
    data["commands"] = kept

    # ---- 物品 ----
    items = []
    item_index = {}
    for item in data["items"]:
        en = item["english"]
        if en in ITEM_REMOVE:
            continue
        if en in ITEM_FIX:
            item["chinese"] = list(ITEM_FIX[en])
        item_index[en] = item
        items.append(item)

    added_items = 0
    for en, aliases in NEW_ITEMS:
        if en in item_index:
            merge_alias(item_index[en], aliases)
        else:
            entry = {"english": en, "chinese": list(aliases)}
            item_index[en] = entry
            items.append(entry)
            added_items += 1
    data["items"] = items

    # ---- 实体 ----
    entities = []
    entity_index = {}
    for entity in data["entities"]:
        en = entity["english"]
        if en in ENTITY_FIX:
            entity["chinese"] = list(ENTITY_FIX[en])
        entity_index[en] = entity
        entities.append(entity)
    added_entities = 0
    for en, aliases in NEW_ENTITIES:
        if en in entity_index:
            merge_alias(entity_index[en], aliases)
        else:
            entry = {"english": en, "chinese": list(aliases)}
            entity_index[en] = entry
            entities.append(entry)
            added_entities += 1
    data["entities"] = entities

    # ---- 效果 ----
    effects = []
    effect_index = {}
    for effect in data["effects"]:
        en = effect["english"]
        if en in EFFECT_REMOVE:
            continue
        if en in EFFECT_FIX:
            effect["chinese"] = list(EFFECT_FIX[en])
        effect_index[en] = effect
        effects.append(effect)
    data["effects"] = effects

    # ---- 附魔 ----
    enchantments = []
    enchant_index = {}
    for enchant in data["enchantments"]:
        en = enchant["english"]
        if en in ENCHANT_FIX:
            new_en, aliases = ENCHANT_FIX[en]
            enchant["english"] = new_en
            enchant["chinese"] = list(aliases)
            en = new_en
        enchant_index[en] = enchant
        enchantments.append(enchant)
    added_enchants = 0
    for en, aliases in NEW_ENCHANTS:
        if en in enchant_index:
            merge_alias(enchant_index[en], aliases)
        else:
            entry = {"english": en, "chinese": list(aliases)}
            enchant_index[en] = entry
            enchantments.append(entry)
            added_enchants += 1
    data["enchantments"] = enchantments

    # ---- 预设 ----
    preset_ids = {p["id"] for p in data["presets"]}
    added_presets = 0
    for pid, category, chinese, english, description in NEW_PRESETS:
        if pid in preset_ids:
            continue
        data["presets"].append({
            "id": pid,
            "category": category,
            "chinese": chinese,
            "english": english,
            "description": description,
        })
        preset_ids.add(pid)
        added_presets += 1

    dump(JAVA, data)

    # ---- 基岩库：补上挪过来的指令 ----
    bed = load(BEDROCK)
    bed_names = {c["name"] for c in bed["commands"]}
    added_bedrock = 0
    for cmd in moved:
        if cmd["name"] in bed_names:
            continue
        # 基岩库不需要 removedIn / requiresPlugin 这类 Java 语义字段
        clean = {k: v for k, v in cmd.items() if k not in ("removedIn", "requiresPlugin")}
        bed["commands"].append(clean)
        bed_names.add(clean["name"])
        added_bedrock += 1
    dump(BEDROCK, bed)

    print("Java 指令 %d 条，删除基岩专属 %d 条，新增 %d 条"
          % (len(data["commands"]), len(JAVA_REMOVE), len(NEW_COMMANDS)))
    print("物品 %d 条（新增 %d）" % (len(data["items"]), added_items))
    print("实体 %d 条（新增 %d）" % (len(data["entities"]), added_entities))
    print("效果 %d 条" % len(data["effects"]))
    print("附魔 %d 条（新增 %d）" % (len(data["enchantments"]), added_enchants))
    print("预设 %d 条（新增 %d）" % (len(data["presets"]), added_presets))
    print("基岩库补入 %d 条指令" % added_bedrock)
    for line in stats:
        print(" -", line)


if __name__ == "__main__":
    main()
