# 内存优化页视觉探针：渲染真实 MemoryOptimizePage + 真实 style.qss
# 用法：qmake memory_page_probe.pro && mingw32-make && release/memory_page_probe.exe
QT       += core gui widgets svg

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = memory_page_probe

# fake 目录在前：#include "utils/ThemeManager.h" 命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../world_map_page_probe/fake $$PWD/../..

SOURCES += memory_page_probe.cpp \
           ../../pages/MemoryOptimizePage.cpp \
           ../../components/OutlinedLabel.cpp \
           ../../components/CollapsibleSectionCard.cpp \
           ../../components/CustomCheckBox.cpp \
           ../../utils/MemoryAllocator.cpp \
           ../../utils/IconHelper.cpp

HEADERS += ../../pages/MemoryOptimizePage.h \
           ../../components/OutlinedLabel.h \
           ../../components/CollapsibleSectionCard.h \
           ../../components/CustomCheckBox.h \
           ../../utils/MemoryAllocator.h \
           ../../utils/IconHelper.h \
           fake/utils/ThemeManager.h \
           fake/utils/SettingsManager.h \
           fake/utils/SystemTools.h

RESOURCES += ../../resources.qrc
