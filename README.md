# 电脑方块盒子 - 多平台支持

## 项目简介
电脑方块盒子是一个基于Qt框架开发的跨平台应用，支持鸿蒙、Windows、macOS和Linux系统。

## 技术栈
- **框架**: Qt 5.15+ / Qt 6.x
- **语言**: C++
- **UI**: Qt Widgets
- **构建工具**: qmake

## 支持的平台
- ✅ Windows
- ✅ macOS
- ✅ Linux
- ✅ HarmonyOS

## 项目结构
```
[c]blockbox/
├── components/       # UI组件
│   ├── sidebar.cpp
│   ├── sidebar.h
│   ├── topbar.cpp
│   └── topbar.h
├── pages/           # 页面
│   ├── browsepage.cpp
│   ├── browsepage.h
│   ├── homepage.cpp
│   ├── homepage.h
│   ├── resourcespage.cpp
│   ├── resourcespage.h
│   ├── settingspage.cpp
│   └── settingspage.h
├── styles/          # 样式表
│   └── style.qss
├── build/           # 构建目录
├── BlockBox.pro     # 项目配置文件
├── main.cpp         # 程序入口
├── mainwindow.cpp   # 主窗口
├── mainwindow.h
├── mainwindow.ui    # UI设计文件
├── platform.cpp     # 平台适配
├── platform.h       # 平台检测宏
└── resources.qrc    # 资源文件
```

## 构建说明

### Windows
1. 安装Qt Creator和MinGW编译器
2. 打开Qt Creator，导入BlockBox.pro项目
3. 选择适当的构建套件（MinGW 64-bit）
4. 构建项目
5. 运行生成的可执行文件

### macOS
1. 安装Qt Creator和Xcode
2. 打开Qt Creator，导入BlockBox.pro项目
3. 选择适当的构建套件（Clang 64-bit）
4. 构建项目
5. 运行生成的应用程序

### Linux
1. 安装Qt开发包和编译工具
   ```bash
   sudo apt-get install qt5-default build-essential
   ```
2. 构建项目
   ```bash
   qmake BlockBox.pro
   make
   ```
3. 运行生成的可执行文件
   ```bash
   ./BlockBoxLinux
   ```

### HarmonyOS
1. 安装鸿蒙开发环境和Qt for HarmonyOS插件
2. 打开Qt Creator，导入BlockBox.pro项目
3. 选择HarmonyOS构建套件
4. 构建项目
5. 部署到鸿蒙设备或模拟器

## 平台特定配置

项目使用条件编译和平台宏来处理不同平台的差异：

- **Windows**: `WINDOWS_OS` 宏
- **macOS**: `MAC_OS` 宏
- **Linux**: `LINUX_OS` 宏
- **HarmonyOS**: `HARMONY_OS` 宏

平台适配功能在 `platform.cpp` 和 `platform.h` 文件中实现，包括：
- 应用数据目录管理
- 配置文件路径处理
- 平台检测函数

## 样式设计
项目使用QSS样式表进行UI美化，样式文件位于 `styles/style.qss`。样式设计考虑了跨平台兼容性，确保在不同平台上都能提供良好的视觉体验。

## 开发注意事项
1. 避免使用平台特定的API，优先使用Qt提供的跨平台API
2. 使用平台宏进行条件编译时，确保代码的可读性和可维护性
3. 测试时应在所有支持的平台上进行验证
4. 资源文件使用Qt资源系统管理，确保在不同平台上都能正确加载

## 许可证
本项目采用MIT许可证。

## 贡献
欢迎提交Issue和Pull Request，共同完善项目的多平台支持。
