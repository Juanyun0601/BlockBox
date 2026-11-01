QT += core
QT -= gui

CONFIG += console c++17
CONFIG -= app_bundle

TARGET = loot_probe
TEMPLATE = app

SOURCES += loot_probe.cpp \
    ../../utils/WorldMap/StructureLoot.cpp

HEADERS += ../../utils/WorldMap/StructureLoot.h

RESOURCES += loot_probe.qrc
