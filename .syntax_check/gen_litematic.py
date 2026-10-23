#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成一个测试用 .litematic（手写 NBT + gzip），用于投影方块编辑器的视觉/性能验证。"""
import gzip, math, struct, sys

TAG_END, TAG_BYTE, TAG_SHORT, TAG_INT, TAG_LONG, TAG_FLOAT, TAG_DOUBLE = 0, 1, 2, 3, 4, 5, 6
TAG_BYTEARR, TAG_STR, TAG_LIST, TAG_COMP, TAG_INTARR, TAG_LONGARR = 7, 8, 9, 10, 11, 12


def s(v):
    b = v.encode('utf-8')
    return struct.pack('>H', len(b)) + b


def tag(t, name, payload):
    return struct.pack('>B', t) + s(name) + payload


def comp(name, children):
    return tag(TAG_COMP, name, b''.join(children) + struct.pack('>B', TAG_END))


def tstr(name, v):   return tag(TAG_STR, name, s(v))
def tint(name, v):   return tag(TAG_INT, name, struct.pack('>i', v))
def tlong(name, v):  return tag(TAG_LONG, name, struct.pack('>q', v))


def tlongarr(name, vals):
    return tag(TAG_LONGARR, name, struct.pack('>i', len(vals)) + b''.join(
        struct.pack('>q', v - (1 << 64) if v >= (1 << 63) else v) for v in vals))


def tlist_comp(name, elems):
    """elems 为「列表元素载荷」（compound 元素=子标签序列+TAG_END，不含类型字节和名字）。"""
    return tag(TAG_LIST, name, struct.pack('>Bi', TAG_COMP, len(elems)) + b''.join(elems))


SX, SY, SZ = 48, 22, 48

palette = []          # list of (name, props dict)
palette_index = {}


def pid(name, props=None):
    key = (name, tuple(sorted((props or {}).items())))
    if key not in palette_index:
        palette_index[key] = len(palette)
        palette.append((name, dict(props or {})))
    return palette_index[key]


AIR = pid('minecraft:air')
STONE = pid('minecraft:stone')
DIRT = pid('minecraft:dirt')
GRASS = pid('minecraft:grass_block', {'snowy': 'false'})
COBBLE = pid('minecraft:cobblestone')
BRICK = pid('minecraft:stone_bricks')
PLANK = pid('minecraft:oak_planks')
LOG = pid('minecraft:oak_log', {'axis': 'y'})
GLASS = pid('minecraft:glass')
WATER = pid('minecraft:water', {'level': '0'})
SAND = pid('minecraft:sand')
LEAVES = pid('minecraft:oak_leaves', {'persistent': 'true', 'distance': '1'})
RED = pid('minecraft:red_wool')
LAMP = pid('minecraft:sea_lantern')
STAIR = pid('minecraft:oak_stairs', {'facing': 'north', 'half': 'bottom', 'shape': 'straight', 'waterlogged': 'false'})
SLAB = pid('minecraft:stone_brick_slab', {'type': 'bottom', 'waterlogged': 'false'})
TORCH = pid('minecraft:torch')
FENCE = pid('minecraft:oak_fence', {'east': 'true', 'north': 'true', 'south': 'false', 'waterlogged': 'false', 'west': 'false'})

cells = [AIR] * (SX * SY * SZ)


def idx(x, y, z):
    return y * (SX * SZ) + z * SX + x


def put(x, y, z, v):
    if 0 <= x < SX and 0 <= y < SY and 0 <= z < SZ:
        cells[idx(x, y, z)] = v


def get(x, y, z):
    if 0 <= x < SX and 0 <= y < SY and 0 <= z < SZ:
        return cells[idx(x, y, z)]
    return AIR


# ---- 地形：起伏草地 + 石层 ----
def height(x, z):
    h = 6.0
    h += 2.2 * math.sin(x * 0.28) * math.cos(z * 0.24)
    h += 1.3 * math.sin((x + z) * 0.15 + 1.1)
    h += 0.9 * math.cos((x - z) * 0.33)
    return max(3, min(SY - 8, int(round(h + 4))))


for x in range(SX):
    for z in range(SZ):
        h = height(x, z)
        for y in range(h - 3):
            put(x, y, z, STONE if y < h - 4 else COBBLE)
        put(x, h - 3, z, DIRT)
        put(x, h - 2, z, DIRT)
        put(x, h - 1, z, GRASS)

# ---- 水池 ----
for x in range(8, 20):
    for z in range(8, 20):
        d = math.hypot(x - 13.5, z - 13.5)
        if d < 5.4:
            h = height(x, z)
            for y in range(h - 3, h + 1):
                put(x, y, z, AIR)
            put(x, h - 3, z, SAND)
            put(x, h - 2, z, WATER)
            put(x, h - 1, z, WATER)

# ---- 小房子（石砖墙 + 木板地板 + 玻璃窗 + 阶梯屋顶）----
BX0, BZ0, BW, BD = 27, 26, 11, 9
BH = max(height(x, z) for x in range(BX0, BX0 + BW) for z in range(BZ0, BZ0 + BD))
BY = BH
for x in range(BX0, BX0 + BW):
    for z in range(BZ0, BZ0 + BD):
        put(x, BY - 1, z, PLANK)
        for y in range(BY, BY + 5):
            edge_x = x in (BX0, BX0 + BW - 1)
            edge_z = z in (BZ0, BZ0 + BD - 1)
            if edge_x or edge_z:
                # 窗户
                win = (y == BY + 2) and ((not edge_x) or x in (BX0, BX0 + BW - 1)) and \
                      ((z - BZ0) in (2, 3, 5, 6) or (x - BX0) in (2, 3, 7, 8))
                put(x, y, z, GLASS if win else BRICK)
            else:
                put(x, y, z, AIR)
# 门洞
for y in range(BY, BY + 3):
    put(BX0 + BW // 2, y, BZ0 + BD - 1, AIR)
# 屋顶（阶梯状）
for layer in range(5):
    y = BY + 5 + layer
    for x in range(BX0 - 1 + layer, BX0 + BW + 1 - layer):
        for z in range(BZ0 - 1 + layer, BZ0 + BD + 1 - layer):
            put(x, y, z, BRICK if layer < 4 else SLAB)
# 屋内装饰
put(BX0 + 2, BY, BZ0 + 2, RED)
put(BX0 + 2, BY + 1, BZ0 + 2, LAMP)
put(BX0 + BW - 3, BY, BZ0 + 2, TORCH)
for x in range(BX0 + 1, BX0 + BW - 1):
    if x % 2 == 0:
        put(x, BY, BZ0 + BD - 2, STAIR)

# ---- 树 ----
def tree(x, z, hgt):
    base = height(x, z) - 1
    for y in range(base, base + hgt):
        put(x, y, z, LOG)
    top = base + hgt - 1
    for dy in range(-1, 3):
        r = 2 if dy < 1 else 1
        for dx in range(-r, r + 1):
            for dz in range(-r, r + 1):
                if abs(dx) == r and abs(dz) == r and dy > 0:
                    continue
                if get(x + dx, top + dy, z + dz) == AIR:
                    put(x + dx, top + dy, z + dz, LEAVES)


for (tx, tz) in [(5, 38), (14, 33), (40, 6), (38, 40), (22, 42)]:
    tree(tx, tz, 6)

# ---- 栅栏小径 ----
for z in range(30, 40):
    put(21, height(21, z) - 1, z, COBBLE)
for z in range(30, 40, 3):
    put(18, height(18, z), z, FENCE)

# ---- 打包 BlockStates ----
n = len(cells)
bits = max(2, int(math.ceil(math.log2(max(2, len(palette))))))
nlongs = (n * bits + 63) // 64
longs = [0] * nlongs
for i, v in enumerate(cells):
    bit = i * bits
    li = bit >> 6
    off = bit & 63
    longs[li] |= (v << off) & 0xFFFFFFFFFFFFFFFF
    if off + bits > 64 and li + 1 < nlongs:
        longs[li + 1] |= v >> (64 - off)

pal_items = []
for (name, props) in palette:
    ch = [tstr('Name', name)]
    if props:
        ch.append(comp('Properties', [tstr(k, str(v)) for k, v in sorted(props.items())]))
    pal_items.append(b''.join(ch) + struct.pack('>B', TAG_END))

region = comp('main', [
    comp('Position', [tint('x', 0), tint('y', 0), tint('z', 0)]),
    comp('Size', [tint('x', SX), tint('y', SY), tint('z', SZ)]),
    tlist_comp('BlockStatePalette', pal_items),
    tlongarr('BlockStates', longs),
    comp('Entities', []),
    comp('TileEntities', []),
    tint('PendingBlockTicks', 0),
    tint('PendingFluidTicks', 0),
])

root = comp('', [
    tint('MinecraftDataVersion', 3700),
    tint('Version', 6),
    comp('Metadata', [
        tstr('Name', '编辑器测试投影'),
        tstr('Author', 'BlockBox'),
        tstr('Description', '地形+水池+小屋+树木，用于验证渲染与交互'),
        tlong('TimeCreated', 0),
        tlong('TimeModified', 0),
        tint('TotalBlocks', 0),
        tint('TotalVolume', SX * SY * SZ),
        tint('RegionCount', 1),
        comp('EnclosingSize', [tint('x', SX), tint('y', SY), tint('z', SZ)]),
    ]),
    comp('Regions', [region]),
])

out = sys.argv[1] if len(sys.argv) > 1 else 'test_proj.litematic'
with gzip.open(out, 'wb') as f:
    f.write(root)
print('wrote', out, 'palette=', len(palette), 'bits=', bits, 'longs=', nlongs, 'cells=', n)
