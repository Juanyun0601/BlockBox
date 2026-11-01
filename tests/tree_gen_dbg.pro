# 树木生成内部诊断探针（include TreeGen.cpp）
QT       += core gui
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = tree_gen_dbg

INCLUDEPATH += .. ../utils/WorldMap ../third_party/cubiomes
SOURCES  += tree_gen_dbg.cpp
LIBS     += -lz
