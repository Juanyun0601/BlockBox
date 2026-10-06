# 视图切换分段控件（ContentViewSwitch）视觉探针：渲染真实组件 + 真实 style.qss
# 用法：qmake view_switch_probe.pro && mingw32-make release && release/view_switch_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = view_switch_probe

# fake 目录在前：#include "utils/ThemeManager.h" 命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../..

SOURCES += view_switch_probe.cpp \
           ../../components/ContentViewSwitch.cpp

HEADERS += ../../components/ContentViewSwitch.h \
           fake/utils/ThemeManager.h
