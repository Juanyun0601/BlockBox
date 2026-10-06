# Fabric 启动链路端到端探针 (真实 GameLauncher/FabricInstaller 目标文件 + 网络可用)
QT       += core network gui widgets
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = java_validate_probe

# 复用主工程已编译的目标文件（private 改 public 的白盒访问仅影响探针 TU 的声明）
PRELINKED_OBJECTS = \
    ../release/GameLauncher.o \
    ../release/GameLauncherCommand.o \
    ../release/GameLauncherPrepare.o \
    ../release/GameLauncherJava.o \
    ../release/moc_GameLauncher.o \
    ../release/FabricInstaller.o \
    ../release/moc_FabricInstaller.o \
    ../release/ErrorAnalyzer.o \
    ../release/moc_ErrorAnalyzer.o \
    ../release/SettingsManager.o \
    ../release/moc_SettingsManager.o \
    ../release/MemoryAllocator.o \
    ../release/LanguageManager.o \
    ../release/moc_LanguageManager.o \
    ../release/DownloadTaskManager.o \
    ../release/moc_DownloadTaskManager.o \
    ../release/JavaScanWorker.o \
    ../release/moc_JavaScanWorker.o \
    ../release/SystemInfo.o \
    ../release/Platform.o \
    ../release/HardwareMonitor.o \
    ../release/moc_HardwareMonitor.o

INCLUDEPATH += ..
SOURCES  += java_validate_probe.cpp
LIBS     += $$PRELINKED_OBJECTS -lpdh -liphlpapi -lws2_32
