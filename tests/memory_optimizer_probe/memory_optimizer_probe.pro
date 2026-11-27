# 内存优化引擎自测探针：跑一次真实的 MemoryOptimizer::optimize() 并打印明细
# 用法：qmake memory_optimizer_probe.pro && mingw32-make && release/memory_optimizer_probe.exe
QT       -= gui
QT       += core

CONFIG   += console c++17
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = memory_optimizer_probe

INCLUDEPATH += $$PWD/../..

SOURCES += memory_optimizer_probe.cpp \
           ../../utils/MemoryOptimizer.cpp

HEADERS += ../../utils/MemoryOptimizer.h
