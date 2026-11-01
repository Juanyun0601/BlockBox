# 投影方块编辑器「灯笼渲染」验证探针：
# 用实例 jar 的真实 blockstate / model / 贴图渲染灯笼，输出面数统计与截图，
# 用于对比灯笼贴图取帧、模型缺面补面两处改动的效果。
# 用法：qmake lantern_probe.pro && mingw32-make
#       release/lantern_probe.exe <实例 versions/版本 目录> <输出目录>
QT       += core gui widgets opengl openglwidgets

CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = lantern_probe

INCLUDEPATH += $$PWD/../.. $$PWD/../../components

HEADERS += ../../components/VoxelViewWidget.h \
           ../../components/GlslCompat.h \
           ../../utils/Schematic/SchematicDocument.h \
           ../../utils/Schematic/InstanceTextureLoader.h \
           ../../utils/Schematic/BlockShapeLoader.h

SOURCES += lantern_probe.cpp \
           ../../components/VoxelViewWidget.cpp \
           ../../utils/Schematic/SchematicDocument.cpp \
           ../../utils/Schematic/InstanceTextureLoader.cpp \
           ../../utils/Schematic/BlockShapeLoader.cpp \
           ../../utils/Schematic/NbtCodec.cpp \
           ../../utils/JarUtils.cpp
