# AI 助手页引用标签圆角探针
# 用法：qmake ref_chip_round_probe.pro && mingw32-make && ./ref_chip_round_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = ref_chip_round_probe

SOURCES += ref_chip_round_probe.cpp
