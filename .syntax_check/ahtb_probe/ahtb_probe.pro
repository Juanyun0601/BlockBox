QT       += core gui widgets
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE = app
TARGET   = ahtb_probe

INCLUDEPATH += $$PWD/../..

SOURCES += main.cpp \
           $$PWD/../../components/AutoHeightTextBrowser.cpp

HEADERS += $$PWD/../../components/AutoHeightTextBrowser.h
