# HarmonyOS / OpenHarmony 部署说明

本目录用于鸿蒙构建产物的汇集与 HAP 打包。`BlockBox.pro` 在识别到鸿蒙构建套件时
（mkspec 含 `ohos`，或手动 `qmake CONFIG+=harmony`），会把编译产物输出到：

```
harmony/entry/libs/arm64-v8a/
```

这与 OpenHarmony 工程的 HAP 打包约定一致：**所有 native so（含 Qt 依赖库与插件）
必须放在 DevEco Studio 工程的 `entry/libs/<abi>/` 目录下**，否则应用启动时会因
找不到 Qt 平台插件（`libplugins_platforms_qopenharmony.so`）而闪退。

## 打包步骤

1. 在 DevEco Studio 中新建一个 **Native C++** 模板工程（Empty Ability 即可），
   将工程目录放置/链接到本目录，或直接把打包所需文件拷入 `entry/`。
2. 构建 BlockBox（使用 Qt for OpenHarmony 套件），产物 so 会自动输出到
   `entry/libs/arm64-v8a/`。
3. 将 Qt for OpenHarmony SDK 中以下内容一并复制到 `entry/libs/arm64-v8a/`：
   - `plugins/platforms/libplugins_platforms_qopenharmony.so`（必需）
   - 应用依赖的 Qt 模块 so（如 `libQt5Widgets.so`、`libQt5Network.so` 等）
   - `plugins/imageformats`、`plugins/iconengines`（SVG 主题图标需要）
4. 在 DevEco Studio 中执行 **Build > Build Hap(s)** 生成 HAP，
   再通过 `hdc install` 或 IDE 部署到设备 / 模拟器。

## 参考资料

- Qt for OpenHarmony 移植项目（OpenHarmony SIG）：
  https://gitee.com/openharmony-sig/qt （镜像：https://gitcode.com/openharmony-sig/qt ）
- Qt Wiki - Qt for OpenHarmony：https://wiki.qt.io/Qt_for_OpenHarmony/zh
- Qt Creator 鸿蒙开发插件（GitHub）：https://github.com/liys-online/qtc-for-harmonyos
- Qt 官方博客 - 为 HarmonyOS 构建 C/C++ 库：
  https://www.qt.io/blog/building-libraries-for-harmonyos-with-vcpkg

## 已知限制

- `Skin3DWidget`（3D 皮肤预览）使用 `#version 330 core` 桌面 GLSL 着色器，
  在仅提供 OpenGL ES 的移动 GPU 上无法编译，鸿蒙端皮肤预览暂不可用，
  需要后续移植为 GLSL ES（`#version 300 es`）。
- Java 版启动依赖 JVM，鸿蒙端需配套 ARM 架构的 JRE 方案（可参考
  PojavLauncher / Fold Craft Launcher 等开源项目的移动端 Java 运行时方案）。
