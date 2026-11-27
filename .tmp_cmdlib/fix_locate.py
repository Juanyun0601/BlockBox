import json
p = r"D:\BlockBox\C-BlockBox\resources\command_database.json"
data = json.loads(open(p, encoding="utf-8").read())
for cmd in data["commands"]:
    if cmd["name"] == "locate":
        new = {}
        for k, v in cmd.items():
            if k == "params":
                new["params"] = [
                    {"name": "类别", "type": "enum", "default": "structure", "required": False,
                     "candidates": ["structure", "biome", "poi"]},
                    {"name": "目标", "type": "string", "required": True},
                ]
            else:
                new[k] = v
        new["syntax"] = "/locate <structure|biome|poi> <目标>"
        cmd.clear(); cmd.update(new)
    if cmd["name"] == "locatebiome":
        new = {}
        for k, v in cmd.items():
            new[k] = v
            if k == "serverOnly":
                new["removedIn"] = "1.19"
        cmd.clear(); cmd.update(new)
text = json.dumps(data, ensure_ascii=False, indent=1).replace("\n", "\r\n") + "\r\n"
open(p, "w", encoding="utf-8", newline="").write(text)
print("locate/locatebiome updated")
