# AI 助手流式渲染视觉探针
# 用法：qmake ai_stream_visual_probe.pro && mingw32-make && ./ai_stream_visual_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = ai_stream_visual_probe

SOURCES += ai_stream_visual_probe.cpp
