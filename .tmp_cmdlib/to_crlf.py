files = [
 r"D:\BlockBox\C-BlockBox\components\InstanceAssistantWindow.cpp",
 r"D:\BlockBox\C-BlockBox\components\InstanceAssistantWindow.h",
 r"D:\BlockBox\C-BlockBox\styles\style.qss",
 r"D:\BlockBox\C-BlockBox\tests\instance_assistant_selftest.cpp",
 r"D:\BlockBox\C-BlockBox\utils\CommandAssistant\CommandCompleter.cpp",
 r"D:\BlockBox\C-BlockBox\utils\CommandAssistant\CommandCompleter.h",
 r"D:\BlockBox\C-BlockBox\utils\CommandAssistant\CommandDatabase.cpp",
 r"D:\BlockBox\C-BlockBox\utils\CommandAssistant\CommandDatabase.h",
 r"D:\BlockBox\C-BlockBox\utils\CommandAssistant\CommandTranslator.cpp",
 r"D:\BlockBox\C-BlockBox\utils\CommandAssistant\CommandTranslator.h",
]
for path in files:
    with open(path, "rb") as fh:
        data = fh.read()
    data = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").replace(b"\n", b"\r\n")
    with open(path, "wb") as fh:
        fh.write(data)
    print("CRLF:", path.split("\\")[-1])
