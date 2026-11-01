# 蘑菇岛伞盖渲染探针：真实种子渲染 region 15,8，断言像素颜色
QT       += core gui concurrent
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = mush_render_probe

INCLUDEPATH += .. ../utils/WorldMap ../third_party/cubiomes
HEADERS  += ../utils/WorldMap/WorldMapRenderer.h \
            ../utils/WorldMap/SeedTerrainRenderer.h \
            ../utils/WorldMap/TreePredictor.h \
            ../utils/WorldMap/CavePredictor.h \
            ../utils/WorldMap/MushroomPredictor.h
SOURCES  += mush_render_probe.cpp \
            ../utils/WorldMap/SeedTerrainRenderer.cpp \
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




