# 「旋转」背景黑底探针
QT       += core gui widgets
CONFIG   += c++17
TEMPLATE  = app
TARGET    = rotating_bg_probe
INCLUDEPATH += .. ../components ../utils
DEFINES  += BUILD_OUT_DIR=\"D:/BlockBox/build_shots/rotating_probe\" SHOT_OUT_DIR=\"D:/BlockBox/build_shots/rotating_probe/shots\"
HEADERS  += ../components/BackgroundWidget.h \
            ../components/PanoramaSkyRenderer.h \
            ../utils/LowConfigMode.h
SOURCES  += ../components/BackgroundWidget.cpp \
            ../components/PanoramaSkyRenderer.cpp \
            ../utils/LowConfigMode.cpp \
            rotating_bg_probe.cpp
RESOURCES += ../resources_panorama.qrc
LIBS     += -luser32 -lgdi32
