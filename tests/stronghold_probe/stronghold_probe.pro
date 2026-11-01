# 要塞预览探针：按种子+起始区块生成要塞模型，导出段包围盒与方块表供逐块对照
# 用法：qmake stronghold_probe.pro && mingw32-make && release/stronghold_probe.exe <seed> <chunkX> <chunkZ> <out.json>
QT       += core

CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = stronghold_probe

INCLUDEPATH += $$PWD/../..

SOURCES += stronghold_probe.cpp \
           ../../utils/WorldMap/StrongholdModel.cpp

HEADERS += ../../utils/WorldMap/StrongholdModel.h
