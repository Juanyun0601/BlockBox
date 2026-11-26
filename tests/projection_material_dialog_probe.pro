# 投影「材料清单」弹窗探针：统计数量 / 中文名 / 筛选 / 复制 / 深浅主题截图
# 用法：qmake projection_material_dialog_probe.pro && mingw32-make && ./debug/projection_material_dialog_probe.exe
QT       += core gui widgets svg network concurrent

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = projection_material_dialog_probe

LIBS     += -lz

RESOURCES += ../style_probe.qrc

ROOT      = ..

INCLUDEPATH += $$ROOT

SOURCES += projection_material_dialog_probe.cpp \
    $$ROOT/components/ProjectionMaterialDialog.cpp \
    $$ROOT/components/AppDialogBase.cpp \
    $$ROOT/utils/ThemeManager.cpp \
    $$ROOT/utils/BackgroundManager.cpp \
    $$ROOT/utils/plugin/PluginManager.cpp \
    $$ROOT/utils/plugin/PluginZip.cpp \
    $$ROOT/utils/CommandAssistant/LitematicReader.cpp \
    $$ROOT/utils/Schematic/NbtCodec.cpp \
    $$ROOT/utils/Schematic/LitematicEditor.cpp \
    $$ROOT/utils/Schematic/SchematicDocument.cpp \
    $$ROOT/utils/Schematic/InstanceBlockNames.cpp \
    $$ROOT/utils/Schematic/InstanceTextureLoader.cpp \
    $$ROOT/utils/JarUtils.cpp

HEADERS += \
    $$ROOT/components/ProjectionMaterialDialog.h \
    $$ROOT/components/AppDialogBase.h \
    $$ROOT/utils/ThemeManager.h \
    $$ROOT/utils/BackgroundManager.h \
    $$ROOT/utils/plugin/PluginManager.h \
    $$ROOT/utils/plugin/PluginZip.h \
    $$ROOT/utils/CommandAssistant/LitematicReader.h \
    $$ROOT/utils/Schematic/NbtCodec.h \
    $$ROOT/utils/Schematic/LitematicEditor.h \
    $$ROOT/utils/Schematic/SchematicDocument.h \
    $$ROOT/utils/Schematic/InstanceBlockNames.h \
    $$ROOT/utils/Schematic/InstanceTextureLoader.h \
    $$ROOT/utils/JarUtils.h

MOC_DIR     = .moc
OBJECTS_DIR = .obj
