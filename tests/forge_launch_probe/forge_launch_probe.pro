# 带加载器实例（Forge / NeoForge）启动链路探针：复用主工程已编译的目标文件，
# 白盒调用真实的 GameLauncher::buildLaunchCommand，并真机拉起 java 验证能进主界面。
#
# 构建（在 release 配置下，与主工程对象一致）：
#   qmake forge_launch_probe.pro -spec win32-g++ "CONFIG+=release"
#   mingw32-make -f Makefile.Release
# 运行：
#   release\forge_launch_probe.exe <版本目录> <java路径>
QT       += core network gui widgets
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = forge_launch_probe

RELEASE_OBJ = $$PWD/../../release

PRELINKED_OBJECTS = \
    $$RELEASE_OBJ/GameLauncher.o \
    $$RELEASE_OBJ/GameLauncherCommand.o \
    $$RELEASE_OBJ/GameLauncherPrepare.o \
    $$RELEASE_OBJ/GameLauncherJava.o \
    $$RELEASE_OBJ/moc_GameLauncher.o \
    $$RELEASE_OBJ/ErrorAnalyzer.o \
    $$RELEASE_OBJ/moc_ErrorAnalyzer.o \
    $$RELEASE_OBJ/SettingsManager.o \
    $$RELEASE_OBJ/moc_SettingsManager.o \
    $$RELEASE_OBJ/MemoryAllocator.o \
    $$RELEASE_OBJ/LanguageManager.o \
    $$RELEASE_OBJ/moc_LanguageManager.o \
    $$RELEASE_OBJ/DownloadTaskManager.o \
    $$RELEASE_OBJ/moc_DownloadTaskManager.o \
    $$RELEASE_OBJ/JavaScanWorker.o \
    $$RELEASE_OBJ/moc_JavaScanWorker.o \
    $$RELEASE_OBJ/SystemInfo.o \
    $$RELEASE_OBJ/Platform.o \
    $$RELEASE_OBJ/HardwareMonitor.o \
    $$RELEASE_OBJ/moc_HardwareMonitor.o

# 说明：GameLauncher.o 里对 FabricInstaller::repairIncompleteVersionJson /
# RuntimeDependencyManager::isSupportedPlatform 的引用与本探针路径无关，
# 由 forge_launch_probe.cpp 提供空实现，避免把整个 GUI 依赖链拖进来。

INCLUDEPATH += $$PWD/../..
SOURCES  += forge_launch_probe.cpp
LIBS     += $$PRELINKED_OBJECTS -lpdh -liphlpapi -lws2_32
