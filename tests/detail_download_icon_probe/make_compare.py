from PIL import Image, ImageDraw, ImageFont

base = r"D:\BlockBox\C-BlockBox\tests\detail_download_icon_probe"
before = Image.open(base + r"\_before.png").convert("RGB")
after = Image.open(base + r"\_after.png").convert("RGB")

pad, gap, label_h = 16, 24, 34
w = pad * 2 + before.width + gap + after.width
h = pad * 2 + before.height + label_h
sheet = Image.new("RGB", (w, h), (18, 18, 18))
d = ImageDraw.Draw(sheet)

try:
    font = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 15)
except Exception:
    font = ImageFont.load_default()

y_img = pad + label_h
sheet.paste(before, (pad, y_img))
sheet.paste(after, (pad + before.width + gap, y_img))

d.text((pad, pad + 6), "修复前：只剩纯色方块", fill=(235, 120, 120), font=font)
d.text((pad + before.width + gap, pad + 6), "修复后：白色下载图标", fill=(130, 220, 150), font=font)

out = base + r"\download_icon_before_after.png"
sheet.save(out)
print("saved", out, sheet.size)
