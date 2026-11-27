import json, io, sys
p = r"D:\BlockBox\C-BlockBox\resources\command_database.json"
raw = open(p, encoding="utf-8").read()
data = json.loads(raw)
out = json.dumps(data, ensure_ascii=False, indent=1) + "\n"
print("identical:", out == raw, "len_raw", len(raw), "len_out", len(out))
if out != raw:
    # find first diff
    for i,(a,b) in enumerate(zip(raw,out)):
        if a != b:
            print("first diff at", i)
            print(repr(raw[max(0,i-80):i+80]))
            print(repr(out[max(0,i-80):i+80]))
            break
