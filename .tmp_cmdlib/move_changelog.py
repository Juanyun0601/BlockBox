p = r"D:\BlockBox\更新日志\方块盒子\beta11.md"
text = open(p, encoding="utf-8", newline="").read().replace("\r\n", "\n")
lines = text.split("\n")

feature_prefixes = [
    "- 实例助手快捷指令里能查到的方块物品",
    "- 数量也可以按平时说话的方式写",
    "- 候选列表里的游戏模式、天气、难度、操作选项",
    "- 常用预设从 80 条加到 113 条",
    "- 快捷指令页的输入框右边多了个「×」清空按钮",
    "- 指令库补了嘎枝、炽足兽、巨人这些生物",
    "- 说的话能听懂的更多了",
]
fix_prefixes = [
    "- 修好了 Java 版快捷指令里混进基岩版专属指令",
    "- 修好了 1.13 就已经删掉的旧指令",
    "- 回家、传送请求、地标传送这些要靠服务器插件",
    "- 修好了「定位 村庄」只翻出半条指令",
    "- 修好了候选列表里「帮助别名",
]

def pull(prefixes):
    got = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if any(line.startswith(pref) for pref in prefixes):
            got.append(line)
            # 连带吃掉紧随其后的一个空行
            if i + 1 < len(lines) and lines[i + 1].strip() == "":
                i += 2
            else:
                i += 1
            continue
        i += 1
    kept = [ln for ln in lines if not any(ln.startswith(pref) for pref in prefixes)]
    return got, kept

features, lines = pull(feature_prefixes)
fixes, lines = pull(fix_prefixes)
assert len(features) == 7, features
assert len(fixes) == 5, fixes

# 去掉连续多个空行（搬走后可能留下双空行）
cleaned = []
for ln in lines:
    if ln.strip() == "" and cleaned and cleaned[-1].strip() == "":
        continue
    cleaned.append(ln)
lines = cleaned

def insert_before_section(lines, heading, block, occurrence):
    hits = [i for i, ln in enumerate(lines) if ln.strip() == heading]
    assert len(hits) >= occurrence, (heading, hits)
    idx = hits[occurrence - 1]
    # 插到该标题前的空行之后（保持段落间距）
    body = []
    for line in block:
        body.append(line)
        body.append("")
    return lines[:idx] + body + lines[idx:]

lines = insert_before_section(lines, "## 优化", features, 1)   # 新增段末尾
lines = insert_before_section(lines, "## 优化", fixes, 2)      # 修复段末尾

text = "\n".join(lines)
text = text.replace("\n", "\r\n")
open(p, "w", encoding="utf-8", newline="").write(text)

# 复核
check = open(p, encoding="utf-8", newline="").read().replace("\r\n", "\n")
sec = [(i, ln) for i, ln in enumerate(check.split("\n")) if ln.startswith("## ")]
print("sections:", sec)
for pref in feature_prefixes[:2] + fix_prefixes[:2]:
    pos = check.index(pref)
    print("offset", pos, "->", pref[:20])
