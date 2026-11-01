# 下界要塞预览探针：按种子+起始区块生成要塞模型，导出段包围盒与方块表供逐块对照
# 用法：qmake fortress_probe.pro && mingw32-make && release/fortress_probe.exe <seed> <chunkX> <chunkZ> <out.json>
QT       += core
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = fortress_probe

INCLUDEPATH += $$PWD/../..

SOURCES += fortress_probe.cpp \
           ../../utils/WorldMap/FortressModel.cpp

HEADERS += ../../utils/WorldMap/FortressModel.h
