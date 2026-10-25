# 整合包 导出→检测→解析 往返验证探针
# 用法：qmake modpack_roundtrip_probe.pro && mingw32-make && ./modpack_roundtrip_probe.exe
QT       += core

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET    = modpack_roundtrip_probe

INCLUDEPATH += ..

SOURCES += \
    modpack_roundtrip_probe.cpp \
    ../utils/modpack/ModpackDetector.cpp \
    ../utils/modpack/ModpackExporter.cpp

HEADERS += \
    ../utils/modpack/ModpackDetector.h \
    ../utils/modpack/ModpackExporter.h \
    ../utils/modpack/ModpackInfo.h

LIBS += -lz
