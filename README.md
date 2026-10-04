# 电脑方块盒子 BlockBox

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

跨平台 Minecraft 启动器（Java 版 / 基岩版），基于 Qt 与 C++ 开发，AI 全程加持。一个盒子装下整个方块世界。

## 功能特性

- **实例管理**：Java 版 / 基岩版实例的创建、导入、多版本切换与启动，支持从其他启动器迁移
- **内容下载**：模组、整合包、资源包、光影，聚合 CurseForge 与 Modrinth 源
- **AI 助手**：AI 对话、崩溃日志智能诊断、指令助手（自然语言转游戏指令）
- **插件系统**：内置插件 SDK 与插件管理，支持本地扩展（见 `sdk/`）
- **皮肤编辑**：内置皮肤编辑器与 3D 预览
- **联机与网络**：局域网联机传输、隧道组网
- **存档管理**：世界存档备份与恢复
- **多语言**：简体中文、繁体中文、英语、西班牙语

## 支持平台

- Windows
- macOS
- Linux
- HarmonyOS
- Android

## 技术栈

- **框架**: Qt 5.15+ / Qt 6.x
- **语言**: C++
- **UI**: Qt Widgets + QSS 主题
- **构建**: qmake

## 项目结构

```
├── main.cpp            # 程序入口
├── MainWindow.cpp      # 主窗口
├── MainWindowPages.cpp # 页面装配
├── platform.cpp/.h     # 平台适配
├── BlockBox.pro        # qmake 工程文件
├── resources.qrc       # Qt 资源清单
├── components/         # UI 组件（对话框、侧边栏、卡片等）
├── pages/              # 各功能页面（含 pages/settings/ 设置页）
├── utils/              # 业务逻辑（启动、下载、认证、模组、插件、网络等）
├── layouts/            # 自定义布局
├── styles/             # QSS 样式主题
├── resources/          # 内置 JSON 数据
├── Images/             # 图标与图片资源
├── translations/       # 多语言翻译（.ts / .qm）
├── sdk/                # 插件 SDK 头文件
├── tests/              # 探针与测试工程
├── android/            # Android 工程资源
└── harmony/            # HarmonyOS 工程说明
```

## 构建说明

### Windows
1. 安装 Qt Creator 和 MinGW（或 MSVC）编译器
2. 打开 Qt Creator，导入 `BlockBox.pro`
3. 选择 64-bit 构建套件，构建并运行

### macOS
1. 安装 Qt Creator 和 Xcode
2. 导入 `BlockBox.pro`，选择 Clang 64-bit 套件
3. 构建并运行

### Linux
```bash
sudo apt-get install qtbase5-dev qt5-qmake build-essential
qmake BlockBox.pro
make
./BlockBox
```

### HarmonyOS
安装鸿蒙开发环境与 Qt for HarmonyOS 插件后，在 Qt Creator 中选择 HarmonyOS 构建套件构建部署。

## API 密钥配置

本仓库**不包含任何第三方 API 密钥**，凭据均在本地配置，不会随代码上传：

- **CurseForge API Key**：请在应用的「设置」中自行填写，保存在本机配置文件中
- **微软账户登录**：`utils/AuthManager.cpp` 中的 `client_id` 为占位符，请在本地填入你自己的值。**请勿将真实密钥提交到仓库**（仓库配置了 pre-push 检查用于拦截）

## 平台适配

跨平台差异通过条件编译处理：`WINDOWS_OS` / `MAC_OS` / `LINUX_OS` / `HARMONY_OS` 宏，具体实现在 `platform.cpp` 与 `platform.h`，包括应用数据目录、配置路径与平台检测。

## 许可证

本项目采用 [MIT 许可证](LICENSE) © Juanyun0601

## 贡献

欢迎提交 Issue 和 Pull Request。
