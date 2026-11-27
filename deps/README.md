# 移动端运行依赖清单

启动器在移动设备（安卓 / 鸿蒙）上检查更新时，除了检查自身版本，还会拉取本目录下的
依赖清单，把「启动器更新」和「运行依赖更新」合并到同一个窗口里展示。用户点
「一键安装运行依赖」后，启动器会下载、校验、解压依赖包到应用数据目录，
不需要用户手动解压或配置。

> **位置要求**：客户端默认读
> `https://raw.githubusercontent.com/Juanyun0601/BlockBox/main/deps/<平台>-runtime.json`，
> 所以本目录必须位于仓库根（`C-BlockBox/deps/`）并推到 `main` 分支。
> 以前这份文件放在仓库外面，线上地址一直是 404，手机端「检查运行依赖」必然失败。

清单按平台分开，因为两边的 Java 运行时不能混用：

| 平台 | 清单文件 | 下载包 abi 键 |
| --- | --- | --- |
| 安卓 | `android-runtime.json` | `arm64-v8a`、`x86_64`、`armeabi-v7a`、`x86` |
| 鸿蒙 | `harmony-runtime.json` | `harmony-arm64-v8a`、`ohos-arm64`、`ohos-arm64-v8a` |

鸿蒙端只认带 `harmony-` / `ohos-` 前缀的架构键，避免把安卓或桌面 Linux 的 JDK 装到鸿蒙设备上
（装上了也跑不起来）。如果某个包两端确实都能用，用 `default` 作为通用键即可。

也可以在设置里把 `runtime_deps_manifest` 设置项改成其他地址（便于本地调试）。

## 字段说明

| 字段 | 说明 |
| --- | --- |
| `id` | 依赖唯一标识，同时作为安装目录名，只能用字母、数字和 `-` |
| `name` | 显示名称 |
| `kind` | `java` 表示装到 Java 运行目录（启动器会自动扫描到），`runtime` 表示装到通用运行库目录 |
| `version` | 最新版本号，客户端用它和本地已安装版本比较 |
| `required` | 是否必需依赖（仅用于展示上的强调） |
| `available` | 有没有可下载的包。**没准备好包时必须写 `false`**，客户端会把该项显示成「官方包尚未发布」并不提供安装按钮；不写默认 `true` |
| `note` | 配合 `available:false` 的一句话说明，会原样显示在界面和更新窗口里 |
| `description` | 一句话说明 |
| `packages` | 按 CPU 架构给下载地址，支持 `arm64-v8a`、`x86_64`、`armeabi-v7a`、`x86`，鸿蒙可用 `harmony-arm64-v8a` / `ohos-arm64`，也可以用 `default` 作为通用项 |
| `packages.*.url` | 依赖包下载地址，支持 `.zip` 与 `.tar.gz` |
| `packages.*.size` | 文件字节数，填 0 表示不校验大小 |
| `packages.*.sha256` | 文件 SHA-256，留空表示不校验；建议填写 |

## 安装位置

- `kind: "java"` → `<应用数据目录>/BlockBox/java/<id>`，启动器扫描 Java 时会把这里每个
  子目录都当作候选运行时（要求存在 `bin/java`）。
- `kind: "runtime"` → `<应用数据目录>/BlockBox/runtime/<id>`。

安装完成后会在目录里写入 `.blockbox-dependency.json` 记录版本，卸载或更新都以它为准。
安装时会自动给 `bin/` 下的文件补上可执行权限。

## 发布新版本

1. 把新的依赖包上传到 GitHub Releases（示例里的地址形如
   `https://github.com/Juanyun0601/BlockBox/releases/download/deps-jre21/harmony-arm64-v8a.tar.gz`）。
2. 修改对应清单里该依赖的 `available`、`note`、`packages`（`url` / `size` / `sha256`）、`version`。
3. 跑一遍 `软件发布/packaging/check_runtime_deps.ps1`，确认清单地址和每个包地址都能访问、
   大小与清单一致（发布前检查里也会自动跑）。
4. 提交到 `main` 分支，用户下次检查更新时就会看到这一项可安装/可更新。

## 已知限制（为什么现在 `available` 全是 false）

Java 版 Minecraft 需要真正的 JVM 才能运行，而手机端现在还没有可用的运行环境：

- **安卓 10 及以上禁止从应用数据目录执行二进制**，所以「下载一个 `bin/java` 再 `exec`」这条路
  在真机上走不通，需要配套的 JNI 方案（用 `JNI_CreateJavaVM` 加载 `libjvm.so`，
  参考 PojavLauncher / Fold Craft Launcher 的做法），启动器目前还没有这部分代码。
- **鸿蒙**是 OpenHarmony 的 musl 环境，必须使用专门构建的 ohos JDK，不能直接用安卓或桌面 Linux 的 JDK。

也就是说，清单、下载、解压、扫描这条链路已经就绪，缺的是「能在手机上真正跑起来的运行时
+ JNI 加载」。在补齐之前，清单里一律写 `available: false` 并给出说明，客户端会如实告诉用户
「官方包尚未发布」，不再让用户点了安装按钮只看到一句失败提示。
