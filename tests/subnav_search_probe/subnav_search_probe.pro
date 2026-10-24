# 子导航面板顶部搜索框渲染探针
# 只做视觉回归：读入已解析 token 的整份 QSS，按 SubNavPanel.cpp 的真实
# objectName / 布局参数重建气泡，逐个状态截图，便于人眼比对改前改后。
# 用法（在本目录）：
#   mkdir build && cd build && qmake ../subnav_search_probe.pro CONFIG+=release && mingw32-make -j4
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = subnav_search_probe

SOURCES += subnav_search_probe.cpp
