# 实例管理概览页视觉探针：渲染真实 InstanceOverviewPage + 真实 style.qss
# 用法：qmake overview_probe.pro && mingw32-make && ./overview_probe.exe
QT       += core gui widgets svg concurrent

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = overview_probe

# fake 目录在前：#include "utils/ThemeManager.h" 命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../..

SOURCES += overview_probe.cpp \
           ../../pages/InstanceOverviewPage.cpp \
           ../../components/OutlinedLabel.cpp \
           ../../utils/IconHelper.cpp

HEADERS += ../../pages/InstanceOverviewPage.h \
           ../../components/OutlinedLabel.h \
           fake/utils/ThemeManager.h

RESOURCES += ../../resources.qrc
