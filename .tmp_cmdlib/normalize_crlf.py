p = r"D:\BlockBox\更新日志\方块盒子\beta11.md"
text = open(p, encoding="utf-8", newline="").read()
text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")
open(p, "w", encoding="utf-8", newline="").write(text)
print("normalized CRLF")
