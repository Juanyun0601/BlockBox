# Java 管理页已安装列表视觉探针
# 用法：qmake java_list_visual_probe.pro && mingw32-make && ./java_list_visual_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = java_list_visual_probe

SOURCES += java_list_visual_probe.cpp
