# 更新安装探针（update_install_probe）

用于验证「把新版本文件拖进方块盒子」这条更新链路的真实行为，覆盖两类官方资产：

- `BlockBox-<版本>-win64-portable.zip`（免安装包，压缩包里还套着一层版本目录）
- `BlockBox-<版本>-win64-setup.exe`（Inno 安装包）

## 为什么需要它

免安装包解压后是 `BlockBox-<版本>-win64-portable/` 一层目录。旧实现直接
`Expand-Archive` 到程序目录，文件全落在子目录里，旧版本一个都没被替换，
表现为「拖进去装不上、关掉启动器就没下文」。

## 怎么跑

```powershell
# 免安装包路径：解压 → 定位程序目录 → 等旧进程退出 → 覆盖 → 重启 → 校验
powershell -ExecutionPolicy Bypass -File .\run_test.ps1

# 安装包路径：先编一个独立 AppId 的测试安装包，再走真实安装流程
powershell -ExecutionPolicy Bypass -File .\run_setup_test.ps1
```

两个脚本都依赖 `build-win-release\release` 下的对象目录（先做一次全量构建）。

## 实现方式

`probe.cpp` 直接 `#include "utils/UpdateChecker.cpp"`，这样匿名命名空间里的
`prepareWindowsUpdate` / `launchWindowsUpdate` 可以照原样调用；链接时排除原来的
`UpdateChecker.o`，其余对象全部复用。探针产物命名为 `BlockBoxWindows.exe` 放进
假程序目录，因此 `applicationFilePath()` 与实际运行时一致。

`marker.exe` 扮演「新版本程序」：被复制成 `BlockBoxWindows.exe` 并启动后，会在
自身目录写 `relaunched.txt`，用来证明更新后确实重启了新版本。

## 校验点

1. 主程序文件被替换成新版本
2. 依赖 DLL / 新增文件按包内容更新
3. `BlockBox\config.ini`、`BlockBox\accounts.json` 等用户数据不被出厂值覆盖
4. `.minecraft` 等游戏数据目录不被改动
5. 更新完成后自动重新打开了新版本
6. 装错架构（arm64 包拖进 x64 版本）会直接报错而不是把程序装坏
