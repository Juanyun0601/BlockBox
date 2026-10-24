// 「新版本」替身：被更新流程复制成 BlockBoxWindows.exe 并启动后，
// 在自身目录写一个标记文件，用来证明更新后确实重启了新版本。
#include <cstdio>
#include <string>
#include <windows.h>

int main()
{
    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0)
        return 1;

    std::string exePath(path);
    const size_t slash = exePath.find_last_of("\\/");
    const std::string dir = slash == std::string::npos ? std::string(".") : exePath.substr(0, slash);
    const std::string markerPath = dir + "\\relaunched.txt";

    FILE *file = std::fopen(markerPath.c_str(), "wb");
    if (!file)
        return 2;
    std::fputs("NEW-VERSION-STARTED", file);
    std::fclose(file);
    return 0;
}
