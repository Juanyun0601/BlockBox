#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成一个带假客户端 jar 的测试实例目录（用于验证方块图标是否取自实例贴图）。

贴图统一做成左半品红、右半蓝色，图标只要来自 jar 就一定能看到这两种颜色。
"""
import os
import sys
import zipfile

from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "tmp_inst")

NAMES = [
    "stone", "glass", "sand", "dirt", "grass_block", "grass_block_top", "grass_block_side",
    "oak_planks", "oak_log", "oak_log_top", "water", "lava", "bricks", "stone_bricks",
    "iron_block", "gold_block", "diamond_block", "redstone_block", "sea_lantern",
    "white_wool", "black_wool", "oak_stairs", "stone_slab", "oak_slab", "oak_fence",
    "oak_door_lower", "oak_door_upper", "oak_trapdoor", "iron_trapdoor",
    "dark_oak_trapdoor", "torch", "wall_torch", "lever", "redstone_wire", "chest",
    "sign", "oak_wall_sign", "cobblestone", "oak_door",
]

ver_dir = os.path.join(OUT, "versions", "1.20.1")
block_dir = os.path.join(OUT, "assets", "minecraft", "textures", "block")
os.makedirs(ver_dir, exist_ok=True)
os.makedirs(block_dir, exist_ok=True)

for name in NAMES:
    img = Image.new("RGBA", (16, 16))
    px = img.load()
    for y in range(16):
        for x in range(16):
            px[x, y] = (255, 0, 255, 255) if x < 8 else (0, 0, 255, 255)
    img.save(os.path.join(block_dir, name + ".png"))

jar = os.path.join(ver_dir, "1.20.1.jar")
with zipfile.ZipFile(jar, "w", zipfile.ZIP_DEFLATED) as z:
    for name in NAMES:
        z.write(os.path.join(block_dir, name + ".png"),
                "assets/minecraft/textures/block/%s.png" % name)

print("instance:", ver_dir)
print("jar:", jar, os.path.getsize(jar), "bytes")
