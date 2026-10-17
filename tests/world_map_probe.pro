# 地表地图渲染探针（合成 region → 真实渲染 → 像素断言 + 并行/渐进性能验证）
QT       += core gui concurrent
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = world_map_probe

INCLUDEPATH += .. ../third_party/cubiomes
HEADERS  += ../utils/WorldMap/WorldMapRenderer.h \
            ../utils/WorldMap/SeedTerrainRenderer.h \
            ../utils/Schematic/NbtCodec.h
SOURCES  += world_map_probe.cpp \
            ../utils/WorldMap/WorldMapRenderer.cpp \
            ../utils/WorldMap/SeedTerrainRenderer.cpp \
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

# 调试符号：崩溃时用 gdb 抓栈
QMAKE_CXXFLAGS_RELEASE = -g -O2
QMAKE_CFLAGS += -fwrapv -O2
QMAKE_LFLAGS += -g
QMAKE_LFLAGS_RELEASE += -g
