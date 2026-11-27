import json
p = r"D:\BlockBox\C-BlockBox\resources\command_database.json"
data = json.loads(open(p, encoding="utf-8").read())
for item in data["items"]:
    if item["english"] == "white_wool" and "羊毛" not in item["chinese"]:
        item["chinese"].append("羊毛")
text = json.dumps(data, ensure_ascii=False, indent=1).replace("\n", "\r\n") + "\r\n"
open(p, "w", encoding="utf-8", newline="").write(text)
print("white_wool aliases:", [i["chinese"] for i in data["items"] if i["english"] == "white_wool"])
