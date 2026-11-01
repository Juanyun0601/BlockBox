# 海底神殿预览探针：按种子生成神殿模型并导出方块表，供与游戏真值逐块对照
# 用法：qmake monument_probe.pro && mingw32-make && release/monument_probe.exe <seed> <x> <z> <version> <out.json>
QT       += core gui

CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = monument_probe

INCLUDEPATH += $$PWD/../.. $$PWD/../../third_party/cubiomes

QMAKE_CFLAGS += -fwrapv -O2
QMAKE_CXXFLAGS += -O2

SOURCES += monument_probe.cpp \
           ../../utils/WorldMap/StructureModelBuilder.cpp \
           ../../utils/WorldMap/StructureTemplates.cpp \
           ../../utils/WorldMap/McRandom.cpp \
           ../../utils/WorldMap/BiomeCatalog.cpp \
           ../../utils/WorldMap/StructureFinder.cpp \
           ../../utils/Schematic/NbtCodec.cpp \
           ../../utils/Schematic/SchematicDocument.cpp \
           ../../third_party/cubiomes/noise.c \
           ../../third_party/cubiomes/biomes.c \
           ../../third_party/cubiomes/layers.c \
           ../../third_party/cubiomes/biomenoise.c \
           ../../third_party/cubiomes/generator.c \
           ../../third_party/cubiomes/finders.c \
           ../../third_party/cubiomes/util.c \
           ../../third_party/cubiomes/quadbase.c

HEADERS += ../../utils/WorldMap/StructureModelBuilder.h \
           ../../utils/WorldMap/StructureTemplates.h \
           ../../utils/WorldMap/McRandom.h \
           ../../utils/WorldMap/BiomeCatalog.h \
           ../../utils/WorldMap/StructureFinder.h \
           ../../utils/Schematic/NbtCodec.h \
           ../../utils/Schematic/SchematicDocument.h

LIBS += -lz
