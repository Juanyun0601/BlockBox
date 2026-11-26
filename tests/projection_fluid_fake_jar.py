"""现造一个最小客户端 jar（只有方块贴图），用来验证投影预览的材质路径。

生成位置：build-win-release/probe_out/fake_instance/fake/fake.jar
里面包含水 / 岩浆 / 石砖 / 玻璃 / 木板 / 下界岩 / 红石 / 红色混凝土 八张贴图。
"""
import os
import random
import zipfile

from PIL import Image, ImageDraw

OUT_DIR = os.path.join("probe_out", "fake_instance", "fake")
JAR = os.path.join(OUT_DIR, "fake.jar")
BLOCK = "assets/minecraft/textures/block/"

random.seed(20261125)


def noise(img, base, spread):
    d = ImageDraw.Draw(img)
    for y in range(16):
        for x in range(16):
            k = random.randint(-spread, spread)
            v = tuple(max(0, min(255, c + k)) for c in base)
            d.point((x, y), v)


def water():
    img = Image.new("RGBA", (16, 16), (200, 205, 215, 255))
    d = ImageDraw.Draw(img)
    for i in range(3):
        y = 3 + i * 5
        d.line([(0, y), (5, y + 1), (10, y - 1), (15, y)], fill=(178, 186, 200, 255))
    noise(img, (205, 210, 220, 255), 12)
    return img


def lava():
    img = Image.new("RGBA", (16, 16), (214, 96, 20, 255))
    d = ImageDraw.Draw(img)
    for (x, y, r) in [(3, 4, 3), (11, 7, 4), (6, 12, 3), (13, 13, 2)]:
        d.ellipse([x - r, y - r, x + r, y + r], fill=(248, 196, 44, 255))
    for (x, y, r) in [(8, 3, 2), (4, 10, 2)]:
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 238, 150, 255))
    return img


def stone_bricks():
    img = Image.new("RGBA", (16, 16), (122, 121, 122, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 15, 15], outline=(96, 96, 96, 255))
    d.line([(0, 7), (15, 7)], fill=(96, 96, 96, 255))
    d.line([(8, 0), (8, 7)], fill=(96, 96, 96, 255))
    d.line([(4, 8), (4, 15)], fill=(96, 96, 96, 255))
    d.line([(12, 8), (12, 15)], fill=(96, 96, 96, 255))
    noise(img, (122, 121, 122, 255), 8)
    return img


def glass():
    img = Image.new("RGBA", (16, 16), (206, 233, 247, 60))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 15, 15], outline=(190, 225, 240, 200))
    d.line([(2, 2), (5, 5)], fill=(235, 245, 252, 200))
    return img


def planks():
    img = Image.new("RGBA", (16, 16), (162, 130, 78, 255))
    d = ImageDraw.Draw(img)
    for y in (5, 10, 15):
        d.line([(0, y), (15, y)], fill=(120, 94, 52, 255))
    for x in (4, 11):
        d.line([(x, 0), (x, 5)], fill=(130, 102, 58, 255))
    noise(img, (162, 130, 78, 255), 10)
    return img


def netherrack():
    img = Image.new("RGBA", (16, 16), (97, 38, 38, 255))
    d = ImageDraw.Draw(img)
    for _ in range(26):
        x, y = random.randint(0, 15), random.randint(0, 15)
        d.point((x, y), (130, 56, 52, 255))
    return img


def redstone():
    img = Image.new("RGBA", (16, 16), (175, 24, 5, 255))
    d = ImageDraw.Draw(img)
    for _ in range(30):
        x, y = random.randint(0, 15), random.randint(0, 15)
        d.point((x, y), (214, 44, 18, 255))
    return img


def red_concrete():
    img = Image.new("RGBA", (16, 16), (160, 39, 34, 255))
    d = ImageDraw.Draw(img)
    for _ in range(24):
        x, y = random.randint(0, 15), random.randint(0, 15)
        d.point((x, y), (185, 56, 48, 255))
    return img


TEXTURES = {
    "water_still": water(),
    "lava_still": lava(),
    "stone_bricks": stone_bricks(),
    "glass": glass(),
    "oak_planks": planks(),
    "netherrack": netherrack(),
    "redstone_block": redstone(),
    "red_concrete": red_concrete(),
}

os.makedirs(OUT_DIR, exist_ok=True)
tmp_dir = os.path.join("probe_out", "_fake_tex")
os.makedirs(tmp_dir, exist_ok=True)

with zipfile.ZipFile(JAR, "w", zipfile.ZIP_DEFLATED) as zf:
    for name, img in TEXTURES.items():
        path = os.path.join(tmp_dir, name + ".png")
        img.save(path)
        zf.write(path, BLOCK + name + ".png")
    zf.writestr("pack.mcmeta",
                '{"pack": {"pack_format": 34, "description": "BlockBox probe"}}')

print("jar:", JAR, os.path.getsize(JAR), "bytes")
for name in TEXTURES:
    print(" ", name)
