# 模组所属关系弹窗探针：依赖树 / 关系图双模式、前置匹配与联动
# 用法：qmake mod_relation_dialog_probe.pro && mingw32-make && ./mod_relation_dialog_probe.exe
QT       += core gui widgets svg network concurrent testlib

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = mod_relation_dialog_probe

LIBS     += -lz

RESOURCES += ../style_probe.qrc \
    mod_relation_dialog_probe.qrc

ROOT      = ..

INCLUDEPATH += $$ROOT

SOURCES += mod_relation_dialog_probe.cpp \
    $$ROOT/components/ModRelationDialog.cpp \
    $$ROOT/components/AppDialogBase.cpp \
    $$ROOT/utils/mod/ModData.cpp \
    $$ROOT/utils/ThemeManager.cpp \
    $$ROOT/utils/BackgroundManager.cpp \
    $$ROOT/utils/IconHelper.cpp \
    $$ROOT/utils/plugin/PluginManager.cpp \
    $$ROOT/utils/plugin/PluginZip.cpp

HEADERS += \
    $$ROOT/components/ModRelationDialog.h \
    $$ROOT/components/AppDialogBase.h \
    $$ROOT/utils/ThemeManager.h \
    $$ROOT/utils/BackgroundManager.h \
    $$ROOT/utils/IconHelper.h \
    $$ROOT/utils/plugin/PluginManager.h

MOC_DIR     = .moc
OBJECTS_DIR = .obj
