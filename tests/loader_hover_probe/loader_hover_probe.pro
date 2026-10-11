# 版本详情页加载器卡片悬浮探针：真实 style.qss + 与 LoaderDetailPage 同构的列表
# 用法：qmake loader_hover_probe.pro && mingw32-make && ./loader_hover_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = loader_hover_probe

SOURCES += loader_hover_probe.cpp
