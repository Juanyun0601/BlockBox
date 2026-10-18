# servers.dat 读写行为探针（纯逻辑，无 GUI）
# 用法：qmake servers_dat_probe.pro && mingw32-make release && release/servers_dat_probe.exe

QT       += core

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = servers_dat_probe

INCLUDEPATH += $$PWD/../..

SOURCES += servers_dat_probe.cpp \
           ../../utils/ServersDatIO.cpp

HEADERS += ../../utils/ServersDatIO.h

LIBS += -lz
