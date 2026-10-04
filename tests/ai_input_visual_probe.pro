# AI 助手页底部输入区视觉探针
# 用法：qmake ai_input_visual_probe.pro && mingw32-make && ./ai_input_visual_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = ai_input_visual_probe

SOURCES += ai_input_visual_probe.cpp
