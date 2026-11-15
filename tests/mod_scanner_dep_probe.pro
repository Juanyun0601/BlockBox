# 模组扫描器依赖解析探针：fabric.mod.json depends/suggests 与 mods.toml [[dependencies]]
# 用法：qmake mod_scanner_dep_probe.pro && mingw32-make && ./mod_scanner_dep_probe.exe
QT       += core gui widgets network concurrent

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = mod_scanner_dep_probe

LIBS     += -lz

ROOT      = ..

INCLUDEPATH += $$ROOT

SOURCES += mod_scanner_dep_probe.cpp \
    $$ROOT/utils/mod/ModScanner.cpp \
    $$ROOT/utils/mod/ModData.cpp \
    $$ROOT/utils/JarUtils.cpp \
    $$ROOT/utils/mod/MurmurHash2.cpp

HEADERS += \
    $$ROOT/utils/mod/ModScanner.h \
    $$ROOT/utils/JarUtils.h

MOC_DIR     = .moc
OBJECTS_DIR = .obj
