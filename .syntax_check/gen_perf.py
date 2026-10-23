#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成较大的测试投影，用于性能验证（尺寸可由参数覆盖）。"""
import gzip, math, struct, sys

TAG_END, TAG_BYTE, TAG_SHORT, TAG_INT, TAG_LONG = 0, 1, 2, 3, 4
TAG_BYTEARR, TAG_STR, TAG_LIST, TAG_COMP, TAG_INTARR, TAG_LONGARR = 7, 8, 9, 10, 11, 12

SX = int(sys.argv[2]) if len(sys.argv) > 2 else 128
SY = int(sys.argv[3]) if len(sys.argv) > 3 else 40
SZ = int(sys.argv[4]) if len(sys.argv) > 4 else 128


def s(v):
    b = v.encode('utf-8')
    return struct.pack('>H', len(b)) + b


def tag(t, name, payload):
    return struct.pack('>B', t) + s(name) + payload


def comp(name, children):
    return tag(TAG_COMP, name, b''.join(children) + struct.pack('>B', TAG_END))


def tstr(n, v): return tag(TAG_STR, n, s(v))
def tint(n, v): return tag(TAG_INT, n, struct.pack('>i', v))
def tlong(n, v): return tag(TAG_LONG, n, struct.pack('>q', v))


def tlongarr(n, vals):
    return tag(TAG_LONGARR, n, struct.pack('>i', len(vals)) + b''.join(
        struct.pack('>q', v - (1 << 64) if v >= (1 << 63) else v) for v in vals))


palette = ['minecraft:air', 'minecraft:stone', 'minecraft:dirt', 'minecraft:grass_block',
           'minecraft:cobblestone', 'minecraft:oak_planks', 'minecraft:glass', 'minecraft:water']
AIR, STONE, DIRT, GRASS, COBBLE, PLANK, GLASS, WATER = range(8)

cells = [AIR] * (SX * SY * SZ)


def idx(x, y, z):
    return y * (SX * SZ) + z * SX + x


def put(x, y, z, v):
    if 0 <= x < SX and 0 <= y < SY and 0 <= z < SZ:
        cells[idx(x, y, z)] = v


def height(x, z):
    h = 14.0
    h += 5.0 * math.sin(x * 0.09) * math.cos(z * 0.11)
    h += 3.0 * math.sin((x + z) * 0.05 + 0.7)
    h += 1.8 * math.cos((x - z) * 0.19)
    h += 1.0 * math.sin(x * 0.41) * math.sin(z * 0.37)
    return max(3, min(SY - 3, int(round(h))))


for x in range(SX):
    for z in range(SZ):
        h = height(x, z)
        for y in range(h):
            v = STONE if y < h - 4 else (DIRT if y < h - 1 else GRASS)
            if (x * 7 + z * 13) % 97 == 0 and y >= h - 4:
                v = COBBLE
            put(x, y, z, v)

# 建筑群：实心块（用于压测面剔除）
for bx in range(8, SX - 16, 24):
    for bz in range(8, SZ - 16, 24):
        base = height(bx, bz)
        for x in range(bx, bx + 12):
            for z in range(bz, bz + 12):
                for y in range(base, base + 9):
                    edge = x in (bx, bx + 11) or z in (bz, bz + 11) or y == base + 8
                    put(x, y, z, PLANK if edge else AIR)
        for y in range(base + 9, base + 10):
            for x in range(bx + 1, bx + 11):
                for z in range(bz + 1, bz + 11):
                    put(x, y, z, GLASS)
        # 内部填充水
        for x in range(bx + 1, bx + 11):
            for z in range(bz + 1, bz + 11):
                for y in range(base, base + 2):
                    if cells[idx(x, y, z)] == AIR:
                        put(x, y, z, WATER)

n = len(cells)
bits = max(2, int(math.ceil(math.log2(len(palette)))))
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
for name in palette:
    pal_items.append(b''.join([tstr('Name', name)]) + struct.pack('>B', TAG_END))

region = comp('main', [
    comp('Position', [tint('x', 0), tint('y', 0), tint('z', 0)]),
    comp('Size', [tint('x', SX), tint('y', SY), tint('z', SZ)]),
    tag(TAG_LIST, 'BlockStatePalette', struct.pack('>Bi', TAG_COMP, len(pal_items)) + b''.join(pal_items)),
    tlongarr('BlockStates', longs),
    comp('Entities', []),
    comp('TileEntities', []),
])

root = comp('', [
    tint('MinecraftDataVersion', 3700),
    tint('Version', 6),
    comp('Metadata', [tstr('Name', 'perf'), tstr('Author', 'BlockBox'), tstr('Description', '')]),
    comp('Regions', [region]),
])

out = sys.argv[1] if len(sys.argv) > 1 else 'perf_proj.litematic'
with gzip.open(out, 'wb') as f:
    f.write(root)
print('wrote', out, f'{SX}x{SY}x{SZ}', 'cells=', n, 'bits=', bits)
