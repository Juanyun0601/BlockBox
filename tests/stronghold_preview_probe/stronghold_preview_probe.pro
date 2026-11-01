# 结构预览构建检查：调用公开接口生成结构模型（含要塞），验证集成可编译可运行
# 用法：qmake && mingw32-make && release/stronghold_preview_probe.exe <structKey> <seed> <x> <z> <version> <out.json>
QT       += core gui

CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = stronghold_preview_probe

INCLUDEPATH += $$PWD/../.. $$PWD/../../third_party/cubiomes

QMAKE_CFLAGS += -fwrapv -O2

SOURCES += stronghold_preview_probe.cpp \
           ../../utils/WorldMap/StructureModelBuilder.cpp \
           ../../utils/WorldMap/StructureTemplates.cpp \
           ../../utils/WorldMap/StrongholdModel.cpp \
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
           ../../utils/WorldMap/StrongholdModel.h \
           ../../utils/WorldMap/McRandom.h \
           ../../utils/WorldMap/BiomeCatalog.h \
           ../../utils/WorldMap/StructureFinder.h \
           ../../utils/Schematic/NbtCodec.h \
           ../../utils/Schematic/SchematicDocument.h

RESOURCES += village_data.qrc

LIBS += -lz
