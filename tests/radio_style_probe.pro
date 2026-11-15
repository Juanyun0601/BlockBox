# 单选框样式探针（加载真实 style.qss，输出亮/暗两套截图）
# 用法：qmake radio_style_probe.pro && mingw32-make && ./radio_style_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = radio_style_probe

SOURCES += radio_style_probe.cpp
