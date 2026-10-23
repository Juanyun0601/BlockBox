# 存档地表地图页压力探针：压「渲染中销毁 / 快速切维度 / 连续重载」等高危路径
# 用法：qmake world_map_stress_probe.pro && mingw32-make && release/world_map_stress_probe.exe [iterations]
QT       += core gui widgets svg concurrent network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = world_map_stress_probe

INCLUDEPATH += $$PWD/../world_map_page_probe/fake $$PWD/../.. $$PWD/../../third_party/cubiomes

SOURCES += world_map_stress_probe.cpp \
           ../../pages/WorldMapPage.cpp \
           ../../utils/WorldMap/WorldMapRenderer.cpp \
           ../../utils/WorldMap/SeedTerrainRenderer.cpp \
           ../../utils/WorldMap/StructureFinder.cpp \
           ../../utils/WorldMap/StructureIcons.cpp \
            ../../utils/WorldMap/BiomeCatalog.cpp \
            ../../utils/WorldMap/BiomeName.cpp \
            ../../utils/WorldMap/OreFinder.cpp \
            ../../utils/JarUtils.cpp \
            ../../utils/Schematic/NbtCodec.cpp \
           ../../utils/Saves/LevelDatEditor.cpp \
           ../../utils/IconHelper.cpp \
           ../../platform.cpp \
           ../../third_party/cubiomes/noise.c \
           ../../third_party/cubiomes/biomes.c \
           ../../third_party/cubiomes/layers.c \
           ../../third_party/cubiomes/biomenoise.c \
           ../../third_party/cubiomes/generator.c \
           ../../third_party/cubiomes/finders.c \
           ../../third_party/cubiomes/util.c \
           ../../third_party/cubiomes/quadbase.c

HEADERS += ../../pages/WorldMapPage.h \
           ../../utils/WorldMap/WorldMapRenderer.h \
           ../../utils/WorldMap/SeedTerrainRenderer.h \
           ../../utils/WorldMap/StructureFinder.h \
           ../../utils/WorldMap/StructureIcons.h \
            ../../utils/WorldMap/BiomeCatalog.h \
            ../../utils/WorldMap/BiomeName.h \
            ../../utils/WorldMap/OreFinder.h \
            ../../utils/JarUtils.h \
            ../../utils/Schematic/NbtCodec.h \
           ../../utils/Saves/LevelDatEditor.h \
           ../world_map_page_probe/fake/utils/ThemeManager.h \
           ../world_map_page_probe/fake/utils/SettingsManager.h \
           ../world_map_page_probe/fake/components/AppFileDialog.h

RESOURCES += ../../resources.qrc
LIBS    += -lz
