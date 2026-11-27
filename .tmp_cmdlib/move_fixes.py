p = r"D:\BlockBox\更新日志\方块盒子\beta11.md"
text = open(p, encoding="utf-8", newline="").read().replace("\r\n", "\n")
lines = text.split("\n")

fix_prefixes = [
    "- 修好了 Java 版快捷指令里混进基岩版专属指令",
    "- 修好了 1.13 就已经删掉的旧指令",
    "- 回家、传送请求、地标传送这些要靠服务器插件",
    "- 修好了「定位 村庄」只翻出半条指令",
    "- 修好了候选列表里「帮助别名",
]
block = [ln for ln in lines if any(ln.startswith(pref) for pref in fix_prefixes)]
assert len(block) == 5, len(block)
kept = [ln for ln in lines if not any(ln.startswith(pref) for pref in fix_prefixes)]

# 把 "# " 这行孤立标题去掉，恢复成连续的修复列表
kept = [ln for ln in kept if ln.strip() != "#"]
# 压缩多余空行
cleaned = []
for ln in kept:
    if ln.strip() == "" and cleaned and cleaned[-1].strip() == "":
        continue
    cleaned.append(ln)
kept = cleaned

# 插到最后一条修复条目之后（「修好了在设置里隐藏…」那行）
anchor = next(i for i, ln in enumerate(kept) if ln.startswith("- 修好了在设置里隐藏「首页"))
body = []
for line in block:
    body.append("")
    body.append(line)
kept = kept[:anchor + 1] + body + kept[anchor + 1:]

text = "\n".join(kept).replace("\n", "\r\n")
open(p, "w", encoding="utf-8", newline="").write(text)
print("moved fixes after anchor; total lines:", len(kept))
