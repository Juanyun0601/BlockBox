# 文字描边重做视觉探针
# 渲染「旧算法 vs 新算法」AI 回答气泡、流式 OutlinedTextEdit，
# 并程序化校验：描边层不扰动正文像素 / 复刻排版与 QLabel 逐像素对齐 /
# 字形本体被完整抠出。输出 PNG 到 D:/BlockBox/_out 供人工核对。
# 构建：qmake outline_redo_probe.pro && mingw32-make && ./outline_redo_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = outline_redo_probe

# fake 目录在前，让 #include "utils/ThemeManager.h" 命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../.. $$PWD/../../components

SOURCES += outline_redo_probe.cpp \
           ../../components/MarkdownOutlinedLabel.cpp \
           ../../components/OutlinedTextEdit.cpp \
           ../../components/TextOutlineLayer.cpp

HEADERS += ../../components/MarkdownOutlinedLabel.h \
           ../../components/OutlinedTextEdit.h \
           ../../components/TextOutlineLayer.h \
           fake/utils/ThemeManager.h
