// 探针用：提供 WinMain -> qMain 的桥接（MinGW 不自动链接 libQt6EntryPoint）
extern int qMain(int, char **);
extern "C" { extern int __argc; extern char **__argv; }
extern "C" int WinMain(void *, void *, char *, int) { return qMain(__argc, __argv); }
