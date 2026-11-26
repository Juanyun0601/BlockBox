# 投影三维预览弹窗「样式」探针：独立编译（只带预览窗口真正用到的那几个源文件）
# 用法（在 build_preview_probe 目录下）：
#   qmake ../projection_preview_style_probe.pro && mingw32-make -j8 && ./debug/projection_preview_style_probe.exe
QT       += core gui widgets svg network concurrent opengl openglwidgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = projection_preview_style_probe

LIBS     += -lz

RESOURCES += projection_preview_probe.qrc

ROOT      = ..

INCLUDEPATH += $$ROOT

SOURCES += projection_preview_style_probe.cpp \
    $$ROOT/components/ProjectionPreviewDialog.cpp \
    $$ROOT/components/ProjectionMaterialDialog.cpp \
    $$ROOT/components/AppDialogBase.cpp \
    $$ROOT/components/AppMessageBox.cpp \
    $$ROOT/components/VoxelViewWidget.cpp \
    $$ROOT/layouts/FlowLayout.cpp \
    $$ROOT/utils/ThemeManager.cpp \
    $$ROOT/utils/BackgroundManager.cpp \
    $$ROOT/utils/LanguageManager.cpp \
    $$ROOT/utils/JarUtils.cpp \
    $$ROOT/utils/plugin/PluginManager.cpp \
    $$ROOT/utils/plugin/PluginZip.cpp \
    $$ROOT/utils/CommandAssistant/LitematicReader.cpp \
    $$ROOT/utils/Schematic/NbtCodec.cpp \
    $$ROOT/utils/Schematic/LitematicEditor.cpp \
    $$ROOT/utils/Schematic/SchematicDocument.cpp \
    $$ROOT/utils/Schematic/InstanceBlockNames.cpp \
    $$ROOT/utils/Schematic/InstanceTextureLoader.cpp \
    $$ROOT/utils/Schematic/InstanceEntityTextures.cpp \
    $$ROOT/utils/Schematic/EntityModelLoader.cpp \
    $$ROOT/utils/Schematic/BlockShapeLoader.cpp

HEADERS += \
    $$ROOT/components/ProjectionPreviewDialog.h \
    $$ROOT/components/ProjectionMaterialDialog.h \
    $$ROOT/components/AppDialogBase.h \
    $$ROOT/components/AppMessageBox.h \
    $$ROOT/components/VoxelViewWidget.h \
    $$ROOT/layouts/FlowLayout.h \
    $$ROOT/utils/ThemeManager.h \
    $$ROOT/utils/BackgroundManager.h \
    $$ROOT/utils/LanguageManager.h \
    $$ROOT/utils/JarUtils.h \
    $$ROOT/utils/plugin/PluginManager.h \
    $$ROOT/utils/plugin/PluginZip.h \
    $$ROOT/utils/CommandAssistant/LitematicReader.h \
    $$ROOT/utils/Schematic/NbtCodec.h \
    $$ROOT/utils/Schematic/LitematicEditor.h \
    $$ROOT/utils/Schematic/SchematicDocument.h \
    $$ROOT/utils/Schematic/InstanceBlockNames.h \
    $$ROOT/utils/Schematic/InstanceTextureLoader.h \
    $$ROOT/utils/Schematic/InstanceEntityTextures.h \
    $$ROOT/utils/Schematic/EntityModelLoader.h \
    $$ROOT/utils/Schematic/BlockShapeLoader.h

MOC_DIR     = .moc
OBJECTS_DIR = .obj
