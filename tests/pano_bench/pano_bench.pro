QT += core gui widgets network svg
CONFIG += console c++17
CONFIG -= app_bundle

TARGET = pano_bench
TEMPLATE = app

INCLUDEPATH += ../..

HEADERS += ../../components/PanoramaSkyRenderer.h \
           ../../components/BackgroundWidget.h \
           ../../utils/LowConfigMode.h

SOURCES += main.cpp \
           ../../components/PanoramaSkyRenderer.cpp \
           ../../components/BackgroundWidget.cpp \
           ../../utils/LowConfigMode.cpp

RESOURCES += ../../resources_panorama.qrc
