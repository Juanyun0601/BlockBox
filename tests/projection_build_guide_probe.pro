# 投影「学习搭建」窗口探针：分层步骤 / 第一步最多材料 / 平面图刻度高亮 / 完成态截图
# 用法：qmake projection_build_guide_probe.pro && mingw32-make && ./debug/projection_build_guide_probe.exe
QT       += core gui widgets svg network concurrent opengl openglwidgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = projection_build_guide_probe

LIBS     += -lz

RESOURCES += ../style_probe.qrc

ROOT      = ..

INCLUDEPATH += $$ROOT

SOURCES += projection_build_guide_probe.cpp \
    $$ROOT/components/ProjectionBuildGuideDialog.cpp \
    $$ROOT/components/LayerBuildPlanWidget.cpp \
    $$ROOT/components/VoxelViewWidget.cpp \
    $$ROOT/utils/StyleKit.cpp \
    $$ROOT/utils/IconHelper.cpp \
    $$ROOT/utils/ThemeManager.cpp \
    $$ROOT/utils/BackgroundManager.cpp \
    $$ROOT/utils/plugin/PluginManager.cpp \
    $$ROOT/utils/plugin/PluginZip.cpp \
    $$ROOT/utils/Schematic/BlockShapeLoader.cpp \
    $$ROOT/utils/Schematic/InstanceTextureLoader.cpp \
    $$ROOT/utils/Schematic/SchematicDocument.cpp \
    $$ROOT/utils/Schematic/NbtCodec.cpp \
    $$ROOT/utils/Schematic/LitematicEditor.cpp \
    $$ROOT/utils/Schematic/InstanceBlockNames.cpp \
    $$ROOT/utils/Schematic/EntityModelLoader.cpp \
    $$ROOT/utils/Schematic/InstanceEntityTextures.cpp \
    $$ROOT/utils/JarUtils.cpp

HEADERS += \
    $$ROOT/components/ProjectionBuildGuideDialog.h \
    $$ROOT/components/LayerBuildPlanWidget.h \
    $$ROOT/components/VoxelViewWidget.h \
    $$ROOT/utils/StyleKit.h \
    $$ROOT/utils/IconHelper.h \
    $$ROOT/utils/ThemeManager.h \
    $$ROOT/utils/BackgroundManager.h \
    $$ROOT/utils/plugin/PluginManager.h \
    $$ROOT/utils/plugin/PluginZip.h \
    $$ROOT/utils/Schematic/BlockShapeLoader.h \
    $$ROOT/utils/Schematic/InstanceTextureLoader.h \
    $$ROOT/utils/Schematic/SchematicDocument.h \
    $$ROOT/utils/Schematic/NbtCodec.h \
    $$ROOT/utils/Schematic/LitematicEditor.h \
    $$ROOT/utils/Schematic/InstanceBlockNames.h \
    $$ROOT/utils/JarUtils.h

MOC_DIR     = .moc
OBJECTS_DIR = .obj
