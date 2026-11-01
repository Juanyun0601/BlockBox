/**
 * @file   StructureLoot.cpp
 * @brief  结构战利品预测：原版战利品表精确概率推算
 * @author BlockBox Team
 * @date   2026-10-31
 *
 * 箱子里的东西由生成结构时掷出的 LootTableSeed 决定，这个种子无法从世界
 * 种子反推，所以逐箱「报菜名」在原理上做不到。这里改用可验证的口径：
 *
 *   1. 按原版算法把战利品表算成每件物品的「出现概率 / 期望数量」：
 *      池 → 轮数 rolls（区间取平均）→ 权重抽条目 → 物品函数（set_count /
 *      附魔 / 药水 / 探险地图 …）→ 嵌套表按边际概率合成；
 *   2. 出现概率 ≥ 95% 的物品标为「必出」——这表示在大量同类箱子中，标了
 *      「必出」的物品至少有 95% 会真的出现（探针测试里逐项统计验证）；
 *   3. 其余物品按概率从高到低排，玩家能一眼看出「稳拿 / 大概率 / 看脸」。
 *
 * 数据取自官方 1.21.4 客户端 data/minecraft/loot_table（宝箱 56 张 +
 * 考古 3 张），解析逻辑对齐原版 LootTable / LootPool / LootPoolEntryContainer。
 */

#include "StructureLoot.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <memory>

namespace StructureLoot {
namespace {

// ═══════════════════════ 文本：物品 / 效果 / 地图名 ═══════════════════════

QMutex g_textMutex;
bool g_textLoaded = false;
QHash<QString, QString> g_zhItems;   ///< 去命名空间 id → 中文名
QHash<QString, QString> g_zhEffects; ///< 效果 id → 中文名
QHash<QString, QString> g_zhTexts;   ///< translate key → 中文文本

/** 读内嵌中文名表（调用前需持有 g_textMutex） */
void ensureTextsLocked()
{
    if (g_textLoaded)
        return;
    g_textLoaded = true;
    QFile f(QStringLiteral(":/utils/WorldMap/loot_item_names.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const auto loadMap = [](const QJsonValue &v, QHash<QString, QString> *dst) {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        {
            const QString value = it.value().toString();
            if (!value.isEmpty())
                dst->insert(it.key(), value);
        }
    };
    loadMap(root.value(QStringLiteral("items")), &g_zhItems);
    loadMap(root.value(QStringLiteral("effects")), &g_zhEffects);
    loadMap(root.value(QStringLiteral("texts")), &g_zhTexts);
}

/** 当前实例物品中文名（客户端 JAR lang） */
QMutex g_itemNamesMutex;
QHash<QString, QString> g_itemZh;
QHash<QString, QString> g_itemEn;

QString stripNs(const QString &id)
{
    return id.startsWith(QLatin1String("minecraft:")) ? id.mid(10) : id;
}

/** 物品显示名：实例 lang → 内嵌中文 → 实例英文 → 资源名 */
QString itemName(const QString &itemId)
{
    const QString id = stripNs(itemId);
    {
        QMutexLocker lock(&g_itemNamesMutex);
        if (!g_itemZh.isEmpty())
        {
            auto it = g_itemZh.constFind(itemId);
            if (it != g_itemZh.constEnd() && !it.value().isEmpty())
                return it.value();
            it = g_itemZh.constFind(QStringLiteral("minecraft:") + id);
            if (it != g_itemZh.constEnd() && !it.value().isEmpty())
                return it.value();
        }
    }
    {
        QMutexLocker lock(&g_textMutex);
        ensureTextsLocked();
        const auto it = g_zhItems.constFind(id);
        if (it != g_zhItems.constEnd() && !it.value().isEmpty())
            return it.value();
    }
    {
        QMutexLocker lock(&g_itemNamesMutex);
        if (!g_itemEn.isEmpty())
        {
            auto it = g_itemEn.constFind(itemId);
            if (it != g_itemEn.constEnd() && !it.value().isEmpty())
                return it.value();
            it = g_itemEn.constFind(QStringLiteral("minecraft:") + id);
            if (it != g_itemEn.constEnd() && !it.value().isEmpty())
                return it.value();
        }
    }
    return id;
}

/** 状态效果中文名 */
QString effectName(const QString &effectId)
{
    const QString id = stripNs(effectId);
    if (id.isEmpty())
        return QString();
    QMutexLocker lock(&g_textMutex);
    ensureTextsLocked();
    const auto it = g_zhEffects.constFind(id);
    if (it != g_zhEffects.constEnd() && !it.value().isEmpty())
        return it.value();
    return id;
}

/** 文本 key 的中文（如 filled_map.buried_treasure） */
QString textName(const QString &key, const QString &fallback)
{
    if (!key.isEmpty())
    {
        QMutexLocker lock(&g_textMutex);
        ensureTextsLocked();
        const auto it = g_zhTexts.constFind(key);
        if (it != g_zhTexts.constEnd() && !it.value().isEmpty())
            return it.value();
    }
    return fallback.isEmpty() ? key : fallback;
}

/**
 * 药水类型 → 「效果 + 等级」：strong_regeneration → 生命恢复 II，
 * long_night_vision → 夜视（延长）。
 */
QString potionText(const QString &potionId)
{
    QString id = stripNs(potionId);
    bool strong = id.startsWith(QLatin1String("strong_"));
    bool prolonged = id.startsWith(QLatin1String("long_"));
    if (strong)
        id = id.mid(7);
    else if (prolonged)
        id = id.mid(5);
    QString eff = effectName(id);
    if (strong)
        eff += QStringLiteral(" II");
    else if (prolonged)
        eff += QStringLiteral("（延长）");
    return eff;
}

// ═══════════════════════ 战利品表 → 中间表示 ═══════════════════════

enum FnKind
{
    FnNone,
    FnSetCount,
    FnEnchant,
    FnPotion,
    FnMap,
    FnName,
    FnStew
};

struct FnSpec
{
    FnKind kind = FnNone;
    int mn = 1;
    int mx = 1;
    bool add = false;
    QString arg;
};

struct TableSpec;

enum EntryKind
{
    EntryItem,
    EntryEmpty,
    EntryNested
};

struct EntrySpec
{
    EntryKind kind = EntryItem;
    QString itemId;
    QString tableId;
    int weight = 1;
    QVector<FnSpec> fns;
    std::shared_ptr<TableSpec> nested;
};

struct PoolSpec
{
    int rmin = 1;
    int rmax = 1;
    double gate = 1.0; ///< 池条件通过概率（random_chance 连乘）
    int totalWeight = 0;
    QVector<EntrySpec> entries;
    QVector<FnSpec> poolFns;
};

struct TableSpec
{
    QVector<PoolSpec> pools;
};

// ── JSON 小工具 ──

int asInt(const QJsonValue &v, int def = 0)
{
    if (v.isDouble())
        return int(v.toDouble());
    if (v.isString())
    {
        bool ok = false;
        const int n = v.toString().toInt(&ok);
        return ok ? n : def;
    }
    return def;
}

/** 轮数 / 数量 provider：常数或区间 */
void readRolls(const QJsonValue &v, int *mn, int *mx)
{
    *mn = 1;
    *mx = 1;
    if (v.isDouble())
    {
        *mn = *mx = int(v.toDouble());
        return;
    }
    if (!v.isObject())
        return;
    const QJsonObject o = v.toObject();
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type.endsWith(QLatin1String("constant")))
    {
        *mn = *mx = asInt(o.value(QStringLiteral("value")), 1);
    }
    else if (type.endsWith(QLatin1String("uniform")))
    {
        *mn = asInt(o.value(QStringLiteral("min")), 1);
        *mx = asInt(o.value(QStringLiteral("max")), *mn);
    }
    else if (type.endsWith(QLatin1String("binomial")))
    {
        // 宝箱表里没有用到；按 0..n 的区间近似
        *mn = 0;
        *mx = asInt(o.value(QStringLiteral("n")), 1);
    }
    if (*mx < *mn)
        *mx = *mn;
}

/** 池 / 条目条件：random_chance 连乘，其余条件视为必过 */
double readConditions(const QJsonArray &conds)
{
    double gate = 1.0;
    for (const QJsonValue &cv : conds)
    {
        const QJsonObject c = cv.toObject();
        const QString type = c.value(QStringLiteral("condition")).toString();
        if (type.endsWith(QLatin1String("random_chance"))
            || type.endsWith(QLatin1String("random_chance_with_luck")))
        {
            const double chance =
                qBound(0.0, c.value(QStringLiteral("chance")).toDouble(1.0), 1.0);
            gate *= chance;
        }
    }
    return gate;
}

FnSpec readFn(const QJsonObject &o)
{
    FnSpec f;
    const QString type = o.value(QStringLiteral("function")).toString();
    if (type.endsWith(QLatin1String("set_count")))
    {
        f.kind = FnSetCount;
        f.add = o.value(QStringLiteral("add")).toBool(false);
        readRolls(o.value(QStringLiteral("count")), &f.mn, &f.mx);
    }
    else if (type.endsWith(QLatin1String("enchant_randomly"))
             || type.endsWith(QLatin1String("enchant_with_levels"))
             || type.endsWith(QLatin1String("set_enchantments")))
    {
        f.kind = FnEnchant;
    }
    else if (type.endsWith(QLatin1String("set_potion")))
    {
        f.kind = FnPotion;
        f.arg = o.value(QStringLiteral("id")).toString();
    }
    else if (type.endsWith(QLatin1String("exploration_map")))
    {
        f.kind = FnMap;
        f.arg = o.value(QStringLiteral("destination")).toString();
    }
    else if (type.endsWith(QLatin1String("set_name")))
    {
        f.kind = FnName;
        f.arg = o.value(QStringLiteral("name"))
                    .toObject()
                    .value(QStringLiteral("translate"))
                    .toString();
    }
    else if (type.endsWith(QLatin1String("set_stew_effect")))
    {
        f.kind = FnStew;
        const QJsonArray effects = o.value(QStringLiteral("effects")).toArray();
        if (!effects.isEmpty())
            f.arg = effects.first().toObject().value(QStringLiteral("id")).toString();
    }
    return f;
}

void readEntries(const QJsonArray &arr, int depth, PoolSpec *p);

/** 单个条目（item / empty / 嵌套表 / 组合）展开进池 */
void readEntry(const QJsonObject &e, int depth, PoolSpec *p)
{
    const QString type =
        e.value(QStringLiteral("type")).toString(QStringLiteral("minecraft:item"));
    const int weight = asInt(e.value(QStringLiteral("weight")), 1);
    if (weight <= 0)
        return;

    if (type.endsWith(QLatin1String("item")))
    {
        EntrySpec s;
        s.kind = EntryItem;
        s.itemId = e.value(QStringLiteral("name")).toString();
        s.weight = weight;
        for (const QJsonValue &fv : e.value(QStringLiteral("functions")).toArray())
            s.fns.append(readFn(fv.toObject()));
        if (!s.itemId.isEmpty())
            p->entries.append(s);
        return;
    }
    if (type.endsWith(QLatin1String("empty")))
    {
        EntrySpec s;
        s.kind = EntryEmpty;
        s.weight = weight;
        p->entries.append(s);
        return;
    }
    if (type.endsWith(QLatin1String("loot_table")))
    {
        EntrySpec s;
        s.kind = EntryNested;
        s.tableId = e.value(QStringLiteral("value")).toString();
        s.weight = weight;
        p->entries.append(s);
        return;
    }
    if (type.endsWith(QLatin1String("alternatives"))
        || type.endsWith(QLatin1String("sequence"))
        || type.endsWith(QLatin1String("group")))
    {
        // 宝箱表未使用；按「子条目摊进同一个池」近似
        readEntries(e.value(QStringLiteral("children")).toArray(), depth + 1, p);
    }
    // tag / dynamic：无法离线展开，跳过
}

void readEntries(const QJsonArray &arr, int depth, PoolSpec *p)
{
    if (depth > 6)
        return;
    for (const QJsonValue &ev : arr)
        readEntry(ev.toObject(), depth, p);
}

// ═══════════════════════ 表加载 / 编译缓存 ═══════════════════════

QMutex g_cacheMutex;
QHash<QString, QJsonObject> g_jsonCache;

QString tablePath(const QString &tableId)
{
    QString p = stripNs(tableId);
    return QStringLiteral(":/utils/WorldMap/loot_tables/") + p + QStringLiteral(".json");
}

QJsonObject loadTableJson(const QString &tableId)
{
    {
        QMutexLocker lock(&g_cacheMutex);
        const auto it = g_jsonCache.constFind(tableId);
        if (it != g_jsonCache.constEnd())
            return it.value();
    }
    QJsonObject obj;
    QFile f(tablePath(tableId));
    if (f.open(QIODevice::ReadOnly))
    {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            obj = doc.object();
    }
    QMutexLocker lock(&g_cacheMutex);
    g_jsonCache.insert(tableId, obj);
    return obj;
}

TableSpec compileTable(const QJsonObject &obj, int depth);

/** 编译嵌套表（递归，深度上限防环） */
std::shared_ptr<TableSpec> compileNested(const QString &tableId, int depth)
{
    if (depth > 4 || tableId.isEmpty())
        return {};
    const QJsonObject sub = loadTableJson(tableId);
    if (sub.isEmpty())
        return {};
    auto spec = std::make_shared<TableSpec>(compileTable(sub, depth + 1));
    return spec;
}

TableSpec compileTable(const QJsonObject &obj, int depth)
{
    TableSpec t;
    const QJsonArray pools = obj.value(QStringLiteral("pools")).toArray();
    for (const QJsonValue &pv : pools)
    {
        const QJsonObject po = pv.toObject();
        PoolSpec p;
        readRolls(po.value(QStringLiteral("rolls")), &p.rmin, &p.rmax);
        p.rmin = qBound(0, p.rmin, 24);
        p.rmax = qBound(p.rmin, p.rmax, 24);
        p.gate = readConditions(po.value(QStringLiteral("conditions")).toArray());
        for (const QJsonValue &fv : po.value(QStringLiteral("functions")).toArray())
            p.poolFns.append(readFn(fv.toObject()));
        readEntries(po.value(QStringLiteral("entries")).toArray(), 0, &p);

        // 嵌套表：编译一次挂在条目上
        for (EntrySpec &e : p.entries)
        {
            if (e.kind == EntryNested)
                e.nested = compileNested(e.tableId, depth);
        }

        p.totalWeight = 0;
        for (const EntrySpec &e : p.entries)
            p.totalWeight += e.weight;
        if (p.totalWeight > 0)
            t.pools.append(p);
    }
    return t;
}

// ═══════════════════════ 条目 → 物品（函数处理） ═══════════════════════

struct Resolved
{
    QString itemId;
    QString note;
    QString display;
    bool ench = false;
    double cntAvg = 1.0;
    int cntMin = 1;
    int cntMax = 1;
};

void applyFn(const FnSpec &f, Resolved *r)
{
    switch (f.kind)
    {
    case FnSetCount:
        r->cntAvg = f.add ? 1.0 + (f.mn + f.mx) * 0.5 : (f.mn + f.mx) * 0.5;
        r->cntMin = f.add ? 1 + f.mn : f.mn;
        r->cntMax = f.add ? 1 + f.mx : f.mx;
        break;
    case FnEnchant:
        r->ench = true;
        break;
    case FnPotion:
        r->note = potionText(f.arg);
        break;
    case FnMap:
        r->display = textName(QStringLiteral("filled_map.buried_treasure"),
                              QStringLiteral("藏宝图"));
        break;
    case FnName:
        r->display = textName(f.arg, QString());
        break;
    case FnStew:
        if (!f.arg.isEmpty())
            r->note = effectName(f.arg);
        break;
    default:
        break;
    }
}

Resolved resolveEntry(const EntrySpec &e, const QVector<FnSpec> &poolFns)
{
    Resolved r;
    r.itemId = e.itemId;
    for (const FnSpec &f : poolFns)
        applyFn(f, &r);
    for (const FnSpec &f : e.fns)
        applyFn(f, &r);
    r.cntAvg = qBound(0.0, r.cntAvg, 64.0);
    r.cntMin = qBound(0, r.cntMin, 64);
    r.cntMax = qBound(r.cntMin, r.cntMax, 64);
    return r;
}

/** 最终显示名 */
QString displayOf(const Resolved &r)
{
    if (!r.display.isEmpty())
        return r.display;
    const QString id = stripNs(r.itemId);
    const bool book = r.ench && id == QLatin1String("book");
    const QString base =
        book ? itemName(QStringLiteral("minecraft:enchanted_book")) : itemName(r.itemId);
    if (!r.note.isEmpty())
        return base + QStringLiteral("（") + r.note + QStringLiteral("）");
    if (r.ench && !book)
        return base + QStringLiteral("（附魔）");
    return base;
}

QString keyOf(const Resolved &r)
{
    return r.itemId + QLatin1Char('\x1f') + r.note + QLatin1Char('\x1f')
           + (r.ench ? QLatin1String("1") : QLatin1String("0")) + QLatin1Char('\x1f')
           + r.display;
}

// ═══════════════════════ 边际概率计算 ═══════════════════════

struct ItemMeta
{
    QString itemId;
    QString note;
    QString display;
    bool ench = false;
};

/** 单次掷取里一件物品的贡献 */
struct RollItem
{
    double p = 0.0;   ///< 这一次掷取里出现的概率
    double exp = 0.0; ///< 这一次掷取的期望数量
    int minC = 1;
    int maxC = 1;
    ItemMeta meta;
};

/** 整张表（一个箱子）里一件物品的分布 */
struct ItemAcc
{
    double pNone = 1.0; ///< 一次都没出现的概率
    double exp = 0.0;   ///< 期望数量
    int minC = 0;
    int maxC = 0;
    ItemMeta meta;
};

using Marg = QHash<QString, ItemAcc>;

/** 轮数 r 在 [rmin,rmax] 均匀分布时，一次都不出的概率 */
double noneOverRolls(double p, int rmin, int rmax)
{
    const int span = rmax - rmin + 1;
    if (span <= 0)
        return 1.0;
    double sum = 0.0;
    for (int r = rmin; r <= rmax; ++r)
        sum += std::pow(1.0 - p, r);
    return sum / double(span);
}

Marg marginalOf(const TableSpec &t, int depth)
{
    Marg out;
    if (depth > 4)
        return out;

    for (const PoolSpec &p : t.pools)
    {
        if (p.totalWeight <= 0 || p.gate <= 0.0)
            continue;
        const double gate = p.gate;
        const double avgRolls = (p.rmin + p.rmax) * 0.5;

        // 先算「一次掷取」里每件物品的边际概率
        QHash<QString, RollItem> perRoll;
        for (const EntrySpec &e : p.entries)
        {
            const double w = double(e.weight) / double(p.totalWeight);
            if (w <= 0.0)
                continue;
            if (e.kind == EntryItem)
            {
                const Resolved r = resolveEntry(e, p.poolFns);
                if (r.itemId.isEmpty() || r.cntAvg <= 0.0)
                    continue;
                RollItem &ri = perRoll[keyOf(r)];
                // 附魔书用「附魔书」贴图（图标跟显示名一致）
                ri.meta.itemId =
                    (r.ench && stripNs(r.itemId) == QLatin1String("book"))
                        ? QStringLiteral("minecraft:enchanted_book")
                        : r.itemId;
                ri.meta.note = r.note;
                ri.meta.display = displayOf(r);
                ri.meta.ench = r.ench;
                ri.p += w;
                ri.exp += w * r.cntAvg;
                ri.minC = r.cntMin;
                ri.maxC = r.cntMax;
            }
            else if (e.kind == EntryNested && e.nested)
            {
                const Marg sub = marginalOf(*e.nested, depth + 1);
                for (auto it = sub.constBegin(); it != sub.constEnd(); ++it)
                {
                    const ItemAcc &sa = it.value();
                    const double sp = 1.0 - sa.pNone;
                    if (sp <= 0.0)
                        continue;
                    RollItem &ri = perRoll[it.key()];
                    if (ri.meta.itemId.isEmpty())
                        ri.meta = sa.meta;
                    ri.p += w * sp;
                    ri.exp += w * sa.exp;
                    ri.minC = sa.minC;
                    ri.maxC = sa.maxC;
                }
            }
        }

        // 再按轮数合成：P(一次都不出) 取区间平均
        for (auto it = perRoll.constBegin(); it != perRoll.constEnd(); ++it)
        {
            const RollItem &ri = it.value();
            const double pRoll = qBound(0.0, ri.p, 1.0);
            const double pNonePool =
                (1.0 - gate) + gate * noneOverRolls(pRoll, p.rmin, p.rmax);
            ItemAcc &a = out[it.key()];
            if (a.meta.itemId.isEmpty())
                a.meta = ri.meta;
            a.pNone *= qBound(0.0, pNonePool, 1.0);
            a.exp += gate * avgRolls * ri.exp;
            a.minC += ri.minC;
            a.maxC += ri.maxC;
        }
    }
    return out;
}

QMutex g_margMutex;
QHash<QString, Marg> g_margCache;

Marg tableMarginal(const QString &tableId)
{
    {
        QMutexLocker lock(&g_margMutex);
        const auto it = g_margCache.constFind(tableId);
        if (it != g_margCache.constEnd())
            return it.value();
    }
    Marg m;
    const QJsonObject obj = loadTableJson(tableId);
    if (!obj.isEmpty())
    {
        const TableSpec spec = compileTable(obj, 0);
        m = marginalOf(spec, 0);
    }
    QMutexLocker lock(&g_margMutex);
    g_margCache.insert(tableId, m);
    return m;
}

// ═══════════════════════ 结构 → 箱子种类 ═══════════════════════

struct PlanRow
{
    const char *table;
    int count;
};

/** 每种结构的宝箱构成（表 id 去命名空间 + 该表的箱子数） */
const QHash<QString, QList<PlanRow>> &structurePlans()
{
    static const QHash<QString, QList<PlanRow>> k = {
        { QStringLiteral("village"),
          { { "chests/village/village_plains_house", 1 },
            { "chests/village/village_desert_house", 1 },
            { "chests/village/village_savanna_house", 1 },
            { "chests/village/village_snowy_house", 1 },
            { "chests/village/village_taiga_house", 1 },
            { "chests/village/village_toolsmith", 1 },
            { "chests/village/village_weaponsmith", 1 },
            { "chests/village/village_armorer", 1 },
            { "chests/village/village_butcher", 1 },
            { "chests/village/village_cartographer", 1 },
            { "chests/village/village_fisher", 1 },
            { "chests/village/village_fletcher", 1 },
            { "chests/village/village_mason", 1 },
            { "chests/village/village_shepherd", 1 },
            { "chests/village/village_tannery", 1 },
            { "chests/village/village_temple", 1 } } },
        { QStringLiteral("outpost"), { { "chests/pillager_outpost", 1 } } },
        { QStringLiteral("mansion"), { { "chests/woodland_mansion", 4 } } },
        { QStringLiteral("stronghold"),
          { { "chests/stronghold_library", 2 },
            { "chests/stronghold_corridor", 1 },
            { "chests/stronghold_crossing", 1 } } },
        { QStringLiteral("desert_pyramid"), { { "chests/desert_pyramid", 4 } } },
        { QStringLiteral("jungle_temple"), { { "chests/jungle_temple", 2 } } },
        { QStringLiteral("igloo"), { { "chests/igloo_chest", 1 } } },
        { QStringLiteral("ocean_ruin"),
          { { "chests/underwater_ruin_big", 1 },
            { "chests/underwater_ruin_small", 1 } } },
        { QStringLiteral("shipwreck"),
          { { "chests/shipwreck_supply", 1 },
            { "chests/shipwreck_treasure", 1 },
            { "chests/shipwreck_map", 1 } } },
        { QStringLiteral("ruined_portal"), { { "chests/ruined_portal", 1 } } },
        { QStringLiteral("ancient_city"),
          { { "chests/ancient_city", 2 }, { "chests/ancient_city_ice_box", 1 } } },
        { QStringLiteral("buried_treasure"), { { "chests/buried_treasure", 1 } } },
        { QStringLiteral("mineshaft"), { { "chests/abandoned_mineshaft", 3 } } },
        { QStringLiteral("trial_chambers"),
          { { "chests/trial_chambers/reward", 3 },
            { "chests/trial_chambers/supply", 1 } } },
        { QStringLiteral("trail_ruins"),
          { { "archaeology/trail_ruins_common", 3 },
            { "archaeology/trail_ruins_rare", 1 } } },
        { QStringLiteral("desert_well"), { { "archaeology/desert_well", 1 } } },
        { QStringLiteral("fortress"), { { "chests/nether_bridge", 2 } } },
        { QStringLiteral("bastion"),
          { { "chests/bastion_treasure", 1 },
            { "chests/bastion_bridge", 1 },
            { "chests/bastion_hoglin_stable", 1 },
            { "chests/bastion_other", 1 } } },
        { QStringLiteral("ruined_portal_nether"), { { "chests/ruined_portal", 1 } } },
        { QStringLiteral("end_city"), { { "chests/end_city_treasure", 2 } } },
        { QStringLiteral("end_ship"), { { "chests/end_city_treasure", 1 } } },
    };
    return k;
}

/** 表 id → 箱子名 */
const QHash<QString, QString> &tableLabels()
{
    static const QHash<QString, QString> k = {
        { QStringLiteral("chests/village/village_plains_house"),
          QStringLiteral("平原小屋") },
        { QStringLiteral("chests/village/village_desert_house"),
          QStringLiteral("沙漠小屋") },
        { QStringLiteral("chests/village/village_savanna_house"),
          QStringLiteral("草原小屋") },
        { QStringLiteral("chests/village/village_snowy_house"),
          QStringLiteral("雪原小屋") },
        { QStringLiteral("chests/village/village_taiga_house"),
          QStringLiteral("针叶林小屋") },
        { QStringLiteral("chests/village/village_toolsmith"),
          QStringLiteral("铁匠铺") },
        { QStringLiteral("chests/village/village_weaponsmith"),
          QStringLiteral("武器匠铺") },
        { QStringLiteral("chests/village/village_armorer"),
          QStringLiteral("盔甲匠铺") },
        { QStringLiteral("chests/village/village_butcher"),
          QStringLiteral("屠夫小屋") },
        { QStringLiteral("chests/village/village_cartographer"),
          QStringLiteral("制图师小屋") },
        { QStringLiteral("chests/village/village_fisher"),
          QStringLiteral("渔夫小屋") },
        { QStringLiteral("chests/village/village_fletcher"),
          QStringLiteral("制箭师小屋") },
        { QStringLiteral("chests/village/village_mason"),
          QStringLiteral("石匠小屋") },
        { QStringLiteral("chests/village/village_shepherd"),
          QStringLiteral("牧羊人小屋") },
        { QStringLiteral("chests/village/village_tannery"),
          QStringLiteral("皮革匠小屋") },
        { QStringLiteral("chests/village/village_temple"),
          QStringLiteral("村庄神庙") },
        { QStringLiteral("chests/pillager_outpost"), QStringLiteral("前哨塔") },
        { QStringLiteral("chests/woodland_mansion"), QStringLiteral("府邸房间") },
        { QStringLiteral("chests/stronghold_library"), QStringLiteral("图书馆") },
        { QStringLiteral("chests/stronghold_corridor"), QStringLiteral("走廊") },
        { QStringLiteral("chests/stronghold_crossing"), QStringLiteral("交叉口") },
        { QStringLiteral("chests/desert_pyramid"), QStringLiteral("神殿宝箱") },
        { QStringLiteral("chests/jungle_temple"), QStringLiteral("神庙宝箱") },
        { QStringLiteral("chests/igloo_chest"), QStringLiteral("地下室") },
        { QStringLiteral("chests/underwater_ruin_big"), QStringLiteral("大废墟") },
        { QStringLiteral("chests/underwater_ruin_small"), QStringLiteral("小废墟") },
        { QStringLiteral("chests/shipwreck_supply"), QStringLiteral("补给箱") },
        { QStringLiteral("chests/shipwreck_treasure"), QStringLiteral("宝藏箱") },
        { QStringLiteral("chests/shipwreck_map"), QStringLiteral("藏宝图箱") },
        { QStringLiteral("chests/ruined_portal"), QStringLiteral("传送门箱") },
        { QStringLiteral("chests/ancient_city"), QStringLiteral("城市宝箱") },
        { QStringLiteral("chests/ancient_city_ice_box"), QStringLiteral("冰窖宝箱") },
        { QStringLiteral("chests/buried_treasure"), QStringLiteral("宝藏箱") },
        { QStringLiteral("chests/abandoned_mineshaft"), QStringLiteral("矿井箱") },
        { QStringLiteral("chests/trial_chambers/reward"), QStringLiteral("奖励箱") },
        { QStringLiteral("chests/trial_chambers/supply"), QStringLiteral("补给箱") },
        { QStringLiteral("archaeology/trail_ruins_common"),
          QStringLiteral("考古（常见）") },
        { QStringLiteral("archaeology/trail_ruins_rare"), QStringLiteral("考古（稀有）") },
        { QStringLiteral("archaeology/desert_well"), QStringLiteral("水井考古") },
        { QStringLiteral("chests/nether_bridge"), QStringLiteral("要塞箱") },
        { QStringLiteral("chests/bastion_treasure"), QStringLiteral("宝藏室") },
        { QStringLiteral("chests/bastion_bridge"), QStringLiteral("桥箱") },
        { QStringLiteral("chests/bastion_hoglin_stable"), QStringLiteral("疣猪兽棚") },
        { QStringLiteral("chests/bastion_other"), QStringLiteral("其他房间") },
        { QStringLiteral("chests/end_city_treasure"), QStringLiteral("末地城箱") },
    };
    return k;
}

QString labelOf(const QString &tableId)
{
    const QString id = stripNs(tableId);
    const auto it = tableLabels().constFind(id);
    if (it != tableLabels().constEnd())
        return it.value();
    // 兜底：取最后一段
    const int slash = id.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? id.mid(slash + 1) : id;
}

/** 出现概率 ≥ 95% 视为「必出」 */
constexpr double kGuaranteedChance = 0.95;

QList<Stack> stacksOfMarginal(const Marg &m)
{
    QList<Stack> out;
    out.reserve(m.size());
    for (auto it = m.constBegin(); it != m.constEnd(); ++it)
    {
        const ItemAcc &a = it.value();
        const double chance = qBound(0.0, 1.0 - a.pNone, 1.0);
        if (chance <= 0.0)
            continue;
        Stack s;
        s.itemId = a.meta.itemId;
        s.name = a.meta.display.isEmpty() ? itemName(a.meta.itemId) : a.meta.display;
        s.note = a.meta.note;
        s.enchanted = a.meta.ench;
        s.chance = chance;
        s.count = qMax(1, qRound(a.exp));
        s.minCount = a.minC;
        s.maxCount = qMax(a.minC, a.maxC);
        s.guaranteed = chance >= kGuaranteedChance;
        out.append(s);
    }
    std::sort(out.begin(), out.end(), [](const Stack &a, const Stack &b) {
        if (a.guaranteed != b.guaranteed)
            return a.guaranteed;
        if (std::abs(a.chance - b.chance) > 1e-9)
            return a.chance > b.chance;
        if (a.count != b.count)
            return a.count > b.count;
        return a.name < b.name;
    });
    return out;
}

} // namespace

// ═══════════════════════ 对外接口 ═══════════════════════

void setClientJar(const QString &jarPath)
{
    Q_UNUSED(jarPath);
}

void setInstanceItemNames(const QHash<QString, QString> &zh,
                          const QHash<QString, QString> &en)
{
    QMutexLocker lock(&g_itemNamesMutex);
    g_itemZh = zh;
    g_itemEn = en;
}

void clearInstanceItemNames()
{
    QMutexLocker lock(&g_itemNamesMutex);
    g_itemZh.clear();
    g_itemEn.clear();
}

Result rollLoot(const QString &structKey, quint64 worldSeed, bool hasSeed,
                qint64 x, qint64 z, int mcMinor)
{
    Q_UNUSED(worldSeed);
    Q_UNUSED(x);
    Q_UNUSED(z);
    Q_UNUSED(mcMinor);

    Result result;
    const auto planIt = structurePlans().constFind(structKey);
    if (planIt == structurePlans().constEnd() || planIt->isEmpty())
        return result;

    // 「任一宝箱」= 按箱子数加权平均（同一结构里随机挑一个箱子）
    int total = 0;
    for (const PlanRow &row : *planIt)
        total += qMax(1, row.count);
    if (total <= 0)
        return result;

    QHash<QString, ItemAcc> mix;
    QHash<QString, ItemAcc> totals;
    QStringList usedTables;
    for (const PlanRow &row : *planIt)
    {
        const QString tableId = QStringLiteral("minecraft:") + QLatin1String(row.table);
        const int count = qMax(1, row.count);
        const Marg m = tableMarginal(tableId);

        ChestLoot cl;
        cl.tableId = tableId;
        cl.label = labelOf(tableId);
        cl.count = count;
        cl.stacks = stacksOfMarginal(m);
        if (!cl.stacks.isEmpty())
            result.chestLoots.append(cl);
        if (!usedTables.contains(tableId))
            usedTables << tableId;
        result.chests += count;

        // 「任一宝箱」：随机挑一个箱子，出现概率按箱子数加权平均
        const double share = double(count) / double(total);
        for (auto it = m.constBegin(); it != m.constEnd(); ++it)
        {
            const ItemAcc &a = it.value();
            const double chance = qBound(0.0, 1.0 - a.pNone, 1.0);
            if (chance <= 0.0)
                continue;

            ItemAcc &acc = mix[it.key()];
            if (acc.meta.itemId.isEmpty())
                acc.meta = a.meta;
            // 暂时用 pNone 存「1 - 加权出现概率」
            acc.pNone -= share * chance;
            acc.exp += a.exp * share;
            if (acc.maxC == 0 || a.minC < acc.minC)
                acc.minC = a.minC;
            acc.maxC = qMax(acc.maxC, a.maxC);

            // 全部宝箱合计：count 个独立箱子都不出的概率
            ItemAcc &tt = totals[it.key()];
            if (tt.meta.itemId.isEmpty())
            {
                tt.meta = a.meta;
                tt.pNone = 1.0;
            }
            tt.pNone *= std::pow(qBound(0.0, a.pNone, 1.0), count);
            tt.exp += a.exp * count;
            tt.minC += a.minC * count;
            tt.maxC += a.maxC * count;
        }
    }

    result.stacks = stacksOfMarginal(mix);
    result.totals = stacksOfMarginal(totals);
    result.fromSeed = hasSeed;
    result.tableIds = usedTables.join(QLatin1Char(','));
    return result;
}

QStringList lootFor(const QString &structKey)
{
    QStringList out;
    const Result r = rollLoot(structKey, 0, false, 0, 0);
    for (const Stack &s : r.stacks)
        out << s.name;
    return out;
}

} // namespace StructureLoot
