QT       += core
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE = app
TARGET   = stairs_probe

INCLUDEPATH += $$PWD/../..

SOURCES += main.cpp \
           $$PWD/../../utils/Schematic/BlockShapeLoader.cpp \
           $$PWD/../../utils/JarUtils.cpp

HEADERS += $$PWD/../../utils/Schematic/BlockShapeLoader.h \
           $$PWD/../../utils/JarUtils.h
