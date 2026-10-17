# 下拉框样式统一性视觉探针：同一行渲染 filterCombo / searchSortCombo / categoryCombo
# 对比三者宽度与像素差异，坐实 QSS 中 searchSortCombo 缺伪元素后缀的 bug
# 用法：qmake combo_style_probe.pro && mingw32-make release && release/combo_style_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = combo_style_probe

SOURCES += combo_style_probe.cpp
