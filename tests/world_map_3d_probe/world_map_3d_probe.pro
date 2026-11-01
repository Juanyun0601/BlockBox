# 存档地图立体视图探针：合成地形 → 真实 WorldMap3DView 渲染 → 截图肉眼验收
# 用法：qmake world_map_3d_probe.pro && mingw32-make && release/world_map_3d_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = world_map_3d_probe

INCLUDEPATH += ../..

SOURCES += world_map_3d_probe.cpp \
           ../../components/WorldMap3DView.cpp

HEADERS += ../../components/WorldMap3DView.h \
           ../../utils/WorldMap/WorldMapRenderer.h
