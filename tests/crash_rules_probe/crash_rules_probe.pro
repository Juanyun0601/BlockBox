# 离线崩溃规则库探针：校验内置规则 JSON、正则合法性与典型崩溃样本的命中结果
QT       += core
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = crash_rules_probe

# 复用主工程已编译的目标文件（相对主工程 release 目录）
PRELINKED_OBJECTS = \
    ../../release/ErrorAnalyzer.o \
    ../../release/moc_ErrorAnalyzer.o \
    ../../release/Platform.o \
    ../../release/qrc_resources_data.o

INCLUDEPATH += ../..
SOURCES  += crash_rules_probe.cpp
LIBS     += $$PRELINKED_OBJECTS
DESTDIR  = .
