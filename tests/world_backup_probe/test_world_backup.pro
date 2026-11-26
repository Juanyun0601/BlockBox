# 存档备份功能独立验证程序（不依赖主程序 GUI / 平台层）
# 用法：qmake test_world_backup.pro && mingw32-make && ./release/test_world_backup.exe
# 链接真实的 utils/WorldBackupManager.cpp 与 PluginZip.cpp，
# SettingsManager 与 Platform::getDataDirectory() 用测试替身。
QT       += core gui concurrent network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = test_world_backup

INCLUDEPATH += ../..

SOURCES += \
    main.cpp \
    fake_settingsmanager.cpp \
    stub_platform.cpp \
    ../../utils/WorldBackupManager.cpp \
    ../../utils/plugin/PluginZip.cpp

HEADERS += \
    ../../utils/WorldBackupManager.h \
    ../../utils/SettingsManager.h \
    ../../utils/plugin/PluginZip.h \
    ../../platform.h
