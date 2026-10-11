# 来源切换分段控件（SourceSwitchBar）视觉探针：渲染真实组件 + 真实 style.qss
# 用法：qmake source_switch_probe.pro && mingw32-make release && release/source_switch_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = source_switch_probe

INCLUDEPATH += $$PWD/..

SOURCES += source_switch_probe.cpp \
           ../components/SourceSwitchBar.cpp

HEADERS += ../components/SourceSwitchBar.h
