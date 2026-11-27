import json
p = r"D:\BlockBox\C-BlockBox\resources\bedrock_command_database.json"
data = json.loads(open(p, encoding="utf-8").read())
hidden = {"?", "w", "tell"}
changed = []
for cmd in data["commands"]:
    if cmd["name"] in hidden and not cmd.get("hidden"):
        new = {}
        for k, v in cmd.items():
            new[k] = v
            if k == "serverOnly":
                new["hidden"] = True
        cmd.clear(); cmd.update(new)
        changed.append(cmd["name"])
text = json.dumps(data, ensure_ascii=False, indent=1).replace("\n", "\r\n") + "\r\n"
open(p, "w", encoding="utf-8", newline="").write(text)
print("bedrock hidden:", changed)
