#############################################################################
# 存档地图模块（运行时从 CDN 下载，不随安装包分发）
#
# 构建约定：
#   - 先构建主程序（BlockBox.pro），链接时会生成导出库：
#       Windows/MinGW : BlockBoxWindows.dll.a（见 BlockBox.pro 的 --out-implib）
#       Linux         : 主程序用 -rdynamic 导出符号，模块加载时动态解析
#   - 再对本工程 qmake + make，传入：
#       APP_IMPLIB=<主程序导出的 .a 绝对路径>   （仅 Windows/MinGW 需要）
#       MODULE_DESTDIR=<模块输出目录>
#     例：
#       qmake worldmap.pro -spec win32-g++ CONFIG+=release \
#             APP_IMPLIB=D:/BlockBox/C-BlockBox/build-win-release/release/BlockBoxWindows.dll.a \
#             MODULE_DESTDIR=D:/BlockBox/C-BlockBox/build-worldmap-release/release
#
# 模块不导出自己的符号表，只负责把世界地图页创建出来；地图页用到的
# 设置、主题、通知、图标、存档解析等仍直接调用主程序里同一份实现，
# 因此模块与主程序必须由同一套 Qt / 编译器构建。
#############################################################################

TEMPLATE = lib
TARGET   = blockbox_worldmap
CONFIG  += shared c++11
CONFIG  -= app_bundle

QT += core gui widgets network concurrent svg
QT += opengl openglwidgets

DEFINES += QT_DEPRECATED_WARNINGS

win32 {
    DEFINES += WINDOWS_OS _WIN32_WINNT=0x0601 WINVER=0x0601
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN
    LIBS += -lpdh -liphlpapi -ldwmapi
    LIBS += -lgdi32 -lole32 -lshell32 -lshlwapi -luuid -lcomdlg32
    LIBS += -luser32 -ladvapi32 -lws2_32 -loleaut32
}
linux:!android:!ohos {
    DEFINES += LINUX_OS
}
macx {
    DEFINES += MAC_OS
    QMAKE_LFLAGS += -Wl,-undefined,dynamic_lookup
}

# zlib：区域文件 / 模板压缩包都直接用 zlib 解压
LIBS += -lz

# 与主程序共用同一份源码树，模块里只编译地图相关的那部分
INCLUDEPATH += $$PWD/../..

# Cubiomes：按种子推算群系 / 结构，种子地形渲染直接依赖；
# 模块自带一份（纯函数，无状态），避免依赖主程序是否恰好链接了某个符号。
INCLUDEPATH += $$PWD/../../third_party/cubiomes
QMAKE_CFLAGS += -fwrapv -O2
SOURCES += \
    ../../third_party/cubiomes/noise.c \
    ../../third_party/cubiomes/biomes.c \
    ../../third_party/cubiomes/layers.c \
    ../../third_party/cubiomes/biomenoise.c \
    ../../third_party/cubiomes/generator.c \
    ../../third_party/cubiomes/finders.c \
    ../../third_party/cubiomes/util.c \
    ../../third_party/cubiomes/quadbase.c

# 主程序导出库（Windows/MinGW）
win32-g++ {
    isEmpty(APP_IMPLIB) {
        error("APP_IMPLIB is required: build BlockBox.pro first, then pass the generated import library path")
    }
    LIBS += $$APP_IMPLIB
}

# 输出目录（脚本传入；留空则用工程默认）
!isEmpty(MODULE_DESTDIR) {
    DESTDIR = $$MODULE_DESTDIR
}

SOURCES += \
    WorldMapModuleExports.cpp \
    ../../pages/WorldMapPage.cpp \
    ../../components/WorldMap3DView.cpp \
    ../../components/MapExportDialog.cpp \
    ../../components/StructureDetailDialog.cpp \
    ../../utils/MapBlockPalette.cpp \
    ../../utils/WorldMap/WorldMapRenderer.cpp \
    ../../utils/WorldMap/SeedTerrainRenderer.cpp \
    ../../utils/WorldMap/TreePredictor.cpp \
    ../../utils/WorldMap/TreeGen.cpp \
    ../../utils/WorldMap/MushroomPredictor.cpp \
    ../../utils/WorldMap/CavePredictor.cpp \
    ../../utils/WorldMap/StructureModelBuilder.cpp \
    ../../utils/WorldMap/StructureTemplates.cpp \
    ../../utils/WorldMap/StructureLoot.cpp \
    ../../utils/WorldMap/StrongholdModel.cpp \
    ../../utils/WorldMap/FortressModel.cpp \
    ../../utils/WorldMap/MineshaftModel.cpp \
    ../../utils/WorldMap/McRandom.cpp \
    ../../utils/WorldMap/BiomeCatalog.cpp \
    ../../utils/WorldMap/BiomeName.cpp \
    ../../utils/WorldMap/OreFinder.cpp

# 地图资料（结构模板 NBT / 战利品表 / 模板池 / 物品中文名）编进模块，
# 桌面端主程序不再包含这些资源（见 BlockBox.pro 的 resources_worldmap.qrc）。
RESOURCES += ../../resources_worldmap.qrc

HEADERS += \
    WorldMapModuleApi.h \
    ../../pages/WorldMapPage.h \
    ../../components/WorldMap3DView.h \
    ../../components/MapExportDialog.h \
    ../../components/StructureDetailDialog.h \
    ../../utils/MapBlockPalette.h \
    ../../utils/WorldMap/WorldMapRenderer.h \
    ../../utils/WorldMap/SeedTerrainRenderer.h \
    ../../utils/WorldMap/TreePredictor.h \
    ../../utils/WorldMap/TreeGen.h \
    ../../utils/WorldMap/TreeData.h \
    ../../utils/WorldMap/MushroomPredictor.h \
    ../../utils/WorldMap/CavePredictor.h \
    ../../utils/WorldMap/StructureModelBuilder.h \
    ../../utils/WorldMap/StructureTemplates.h \
    ../../utils/WorldMap/StructureLoot.h \
    ../../utils/WorldMap/StrongholdModel.h \
    ../../utils/WorldMap/FortressModel.h \
    ../../utils/WorldMap/MineshaftModel.h \
    ../../utils/WorldMap/McRandom.h \
    ../../utils/WorldMap/BiomeCatalog.h \
    ../../utils/WorldMap/BiomeName.h \
    ../../utils/WorldMap/OreFinder.h
