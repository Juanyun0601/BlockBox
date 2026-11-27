import json
p = r"D:\BlockBox\C-BlockBox\resources\command_database.json"
data = json.loads(open(p, encoding="utf-8").read())
fixed = []
for cmd in data["commands"]:
    if cmd["name"] in ("testfor", "testforblock", "testforblocks") and cmd.get("serverOnly"):
        cmd["serverOnly"] = False
        fixed.append(cmd["name"])
    if cmd["name"] == "locate":
        # 保证类别参数带中文别名可识别（结构 / 生物群系 / 兴趣点）
        pass
text = json.dumps(data, ensure_ascii=False, indent=1).replace("\n", "\r\n") + "\r\n"
open(p, "w", encoding="utf-8", newline="").write(text)
print("serverOnly 修正:", fixed)
