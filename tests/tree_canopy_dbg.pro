# 树冠连通块诊断探针（include TreeGen.cpp + SeedTerrainRenderer.cpp）
QT       += core gui concurrent
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = tree_canopy_dbg
DEFINES  += BB_TREE_STATS

INCLUDEPATH += .. ../utils/WorldMap ../third_party/cubiomes
HEADERS  += ../utils/WorldMap/WorldMapRenderer.h \
            ../utils/WorldMap/SeedTerrainRenderer.h \
            ../utils/WorldMap/TreePredictor.h \
            ../utils/WorldMap/TreeGen.h \
            ../utils/WorldMap/CavePredictor.h \
            ../utils/WorldMap/MushroomPredictor.h
SOURCES  += tree_canopy_dbg.cpp \
            ../utils/WorldMap/TreePredictor.cpp \
            ../utils/WorldMap/CavePredictor.cpp \
            ../utils/WorldMap/MushroomPredictor.cpp \
            ../utils/WorldMap/McRandom.cpp \
            ../utils/WorldMap/WorldMapRenderer.cpp \
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
