# 勋章分享图探针：独立编译 MedalShareCard，真实出图检查版式
QT       += core gui widgets svg
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = medal_share_probe

INCLUDEPATH += .. ../utils

RESOURCES += medal_share_probe.qrc

HEADERS  += ../utils/MedalShareCard.h \
            ../utils/MedalCatalog.h \
            ../utils/IconHelper.h
SOURCES  += medal_share_probe.cpp \
            ../utils/MedalShareCard.cpp \
            ../utils/MedalCatalog.cpp \
            ../utils/IconHelper.cpp

MOC_DIR     = .moc
OBJECTS_DIR = .obj
