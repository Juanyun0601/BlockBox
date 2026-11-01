# -*- coding: utf-8 -*-
"""独立校验战利品概率：用另一套实现逐箱模拟，和 C++ 探针输出比对。

用法：先跑 loot_probe.exe 生成 _out/loot_probe.tsv，再执行本脚本。
"""
import collections
import io
import json
import os
import random
import sys

ROOT = r"D:\BlockBox\C-BlockBox\utils\WorldMap\loot_tables"
NAMES = r"D:\BlockBox\C-BlockBox\utils\WorldMap\loot_item_names.json"
TSV = r"D:\BlockBox\_out\loot_probe.tsv"

names = json.load(io.open(NAMES, encoding="utf-8"))
effects = names["effects"]
texts = names["texts"]
_table_cache = {}


def load(table_id):
    rel = table_id.replace("minecraft:", "")
    if rel not in _table_cache:
        path = os.path.join(ROOT, rel.replace("/", os.sep) + ".json")
        with io.open(path, encoding="utf-8") as f:
            _table_cache[rel] = json.load(f)
    return _table_cache[rel]


def rolls(v):
    if isinstance(v, (int, float)):
        return int(v), int(v)
    if not isinstance(v, dict):
        return 1, 1
    t = v.get("type", "")
    if t.endswith("uniform"):
        return int(v.get("min", 1)), int(v.get("max", 1))
    if t.endswith("constant"):
        n = int(v.get("value", 1))
        return n, n
    if t.endswith("binomial"):
        return 0, int(v.get("n", 1))
    return 1, 1


def potion_text(pid):
    pid = pid.replace("minecraft:", "")
    strong = pid.startswith("strong_")
    prolonged = pid.startswith("long_")
    if strong:
        pid = pid[7:]
    elif prolonged:
        pid = pid[5:]
    eff = effects.get(pid, pid)
    if strong:
        eff += " II"
    elif prolonged:
        eff += "（延长）"
    return eff


def resolve(entry, pool_fns):
    item = entry.get("name", "")
    note = ""
    ench = False
    disp = ""
    for fn in list(pool_fns) + list(entry.get("functions", [])):
        t = fn.get("function", "")
        if t.endswith(("enchant_randomly", "enchant_with_levels", "set_enchantments")):
            ench = True
        elif t.endswith("set_potion"):
            note = potion_text(fn.get("id", ""))
        elif t.endswith("exploration_map"):
            disp = texts.get("filled_map.buried_treasure", "藏宝图")
        elif t.endswith("set_name"):
            key = fn.get("name", {}).get("translate", "")
            disp = texts.get(key, "藏宝图")
        elif t.endswith("set_stew_effect"):
            effs = fn.get("effects") or []
            if effs:
                eid = effs[0].get("id", "").replace("minecraft:", "")
                note = effects.get(eid, "")
    if ench and item == "minecraft:book":
        # C++ 侧只改显示名，itemId 仍是 book
        pass
    return item, note, ench, disp


def roll_table(table, rnd, acc, depth=0):
    if depth > 6:
        return
    for pool in table.get("pools", []):
        gate = 1.0
        for c in pool.get("conditions", []):
            if c.get("condition", "").endswith("random_chance"):
                gate *= c.get("chance", 1.0)
        if gate < 1.0 and rnd.random() >= gate:
            continue
        lo, hi = rolls(pool.get("rolls", 1))
        n = rnd.randint(lo, hi)
        entries = [e for e in pool.get("entries", []) if e.get("weight", 1) > 0]
        total = sum(e.get("weight", 1) for e in entries)
        if total <= 0:
            continue
        for _ in range(n):
            r = rnd.uniform(0, total)
            chosen = entries[-1]
            for e in entries:
                r -= e.get("weight", 1)
                if r < 0:
                    chosen = e
                    break
            t = chosen.get("type", "minecraft:item")
            if t.endswith("empty"):
                continue
            if t.endswith("loot_table"):
                roll_table(load(chosen["value"]), rnd, acc, depth + 1)
            elif t.endswith("item"):
                item, note, ench, disp = resolve(chosen, pool.get("functions", []))
                acc.add((item, note, bool(ench)))


def main():
    samples = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    predicted = collections.defaultdict(dict)  # table -> key -> (chance, guaranteed)
    with io.open(TSV, encoding="utf-8") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 9:
                continue
            _struct, table, item, note, chance, count, guar, ench, name = parts
            # 探针里附魔书用「附魔书」贴图 id 展示，比对时还原成 book
            if item == "minecraft:enchanted_book" and ench == "1":
                item = "minecraft:book"
            key = (item, note, ench == "1")
            prev = predicted[table].get(key)
            value = (float(chance), guar == "1", int(count), name)
            if prev is None or value[0] > prev[0]:
                predicted[table][key] = value

    rnd = random.Random(20261031)
    worst = 0.0
    worst_key = None
    mismatches = 0
    guaranteed_total = 0
    guaranteed_ok = 0
    rows = 0
    for table, keys in sorted(predicted.items()):
        hits = collections.Counter()
        for _ in range(samples):
            seen = set()
            roll_table(load(table), rnd, seen)
            for k in seen:
                hits[k] += 1
        for key, (chance, guar, count, name) in keys.items():
            sim = hits.get(key, 0)
            rate = sim / float(samples)
            diff = abs(rate - chance)
            rows += 1
            if diff > worst:
                worst, worst_key = diff, (table, key, chance, rate)
            if diff > 0.02:
                mismatches += 1
                print("  [MISMATCH] %s %s pred=%.3f sim=%.3f" % (table, name, chance, rate))
            if guar:
                guaranteed_total += 1
                if rate >= 0.95:
                    guaranteed_ok += 1
                else:
                    print("  [LOW] %s %s marked 必出 but hit %.3f" % (table, name, rate))

    print("rows=%d  mismatches=%d  worst=%.4f %s" % (rows, mismatches, worst, worst_key))
    print("guaranteed items: %d/%d hit >=95%%" % (guaranteed_ok, guaranteed_total))
    return 0 if mismatches == 0 and guaranteed_ok == guaranteed_total else 1


if __name__ == "__main__":
    sys.exit(main())
