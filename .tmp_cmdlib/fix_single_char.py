import json, io
p = r"D:\BlockBox\C-BlockBox\resources\command_database.json"
raw = open(p, encoding="utf-8").read()
data = json.loads(raw)
changed = []
for cmd in data["commands"]:
    if cmd["name"] == "give" and "给" in cmd["chinese"]:
        cmd["chinese"].remove("给"); changed.append("give: 给")
    if cmd["name"] == "kick" and "踢" in cmd["chinese"]:
        cmd["chinese"].remove("踢"); changed.append("kick: 踢")
text = json.dumps(data, ensure_ascii=False, indent=1).replace("\n", "\r\n") + "\r\n"
open(p, "w", encoding="utf-8", newline="").write(text)
print("removed:", changed)
