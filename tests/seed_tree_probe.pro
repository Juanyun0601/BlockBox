# 种子树冠探针：方块级渲染 region + 雪地扫描，检查未加载区块的预测树木
QT       += core gui concurrent
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = seed_tree_probe

INCLUDEPATH += .. ../utils/WorldMap ../third_party/cubiomes
HEADERS  += ../utils/WorldMap/WorldMapRenderer.h \
            ../utils/WorldMap/SeedTerrainRenderer.h \
            ../utils/WorldMap/TreePredictor.h \
            ../utils/WorldMap/TreeGen.h \
            ../utils/WorldMap/CavePredictor.h \
            ../utils/WorldMap/MushroomPredictor.h
SOURCES  += seed_tree_probe.cpp \
            ../utils/WorldMap/SeedTerrainRenderer.cpp \
            ../utils/WorldMap/TreePredictor.cpp \
            ../utils/WorldMap/TreeGen.cpp \
            ../utils/WorldMap/CavePredictor.cpp \
            ../utils/WorldMap/MushroomPredictor.cpp \
            ../utils/WorldMap/McRandom.cpp \
            ../utils/WorldMap/WorldMapRenderer.cpp \
            ../utils/MapBlockPalette.cpp \
            ../utils/Schematic/NbtCodec.cpp \
            ../third_party/cubiomes/noise.c \
            ../third_party/cubiomes/biomes.c \
            ../third_party/cubiomes/layers.c \
            ../third_party/cubiomes/biomenoise.c \
            ../third_party/cubiomes/generator.c \
            ../third_party/cubiomes/finders.c \
            ../third_party/cubiomes/util.c \
            ../third_party/cubiomes/quadbase.c
LIBS     += -lz
