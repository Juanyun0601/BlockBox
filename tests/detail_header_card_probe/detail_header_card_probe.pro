# 资源详情页顶部信息区卡片（QWidget#modDetailHeader）渲染探针
# 独立于应用主体：不链接 ThemeManager，自己读 styles/style.qss + utils/ThemeManager.cpp 取真实 token
# 用法：mkdir build && cd build && qmake ../detail_header_card_probe.pro CONFIG+=release && mingw32-make -j4
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = detail_header_card_probe

INCLUDEPATH += $$PWD/..

SOURCES += detail_header_card_probe.cpp
