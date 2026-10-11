# 按种子算结构位置探针：读真实存档的种子/版本 → cubiomes 查询 → 输出坐标
# 用法：qmake structure_probe.pro && mingw32-make && release/structure_probe.exe <存档目录>
QT       += core
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = structure_probe

INCLUDEPATH += $$PWD/../.. $$PWD/../../third_party/cubiomes

QMAKE_CFLAGS += -fwrapv -O2

SOURCES += structure_probe.cpp \
           ../../utils/WorldMap/StructureFinder.cpp \
           ../../utils/Saves/LevelDatEditor.cpp \
           ../../third_party/cubiomes/noise.c \
           ../../third_party/cubiomes/biomes.c \
           ../../third_party/cubiomes/layers.c \
           ../../third_party/cubiomes/biomenoise.c \
           ../../third_party/cubiomes/generator.c \
           ../../third_party/cubiomes/finders.c \
           ../../third_party/cubiomes/util.c \
           ../../third_party/cubiomes/quadbase.c

LIBS += -lz
