# 下拉框外观对比探针：把真实 style.qss 里的 QComboBox 规则取出来，
# 在同一张卡片上并排渲染「存档地图工具栏（参考） / 各页面现状 / 候选变体」，
# 用来看清每种外观的形状、留白与配色差异。
# 用法：qmake combo_look_probe.pro && mingw32-make -f Makefile.Release && release/combo_look_probe.exe
QT       += core gui widgets svg

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = combo_look_probe

SOURCES += combo_look_probe.cpp
