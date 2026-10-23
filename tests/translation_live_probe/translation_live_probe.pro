QT += core network
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = translation_live_probe
TEMPLATE = app

INCLUDEPATH += $$PWD/../..

SOURCES += main.cpp \
    $$PWD/../../utils/translation/TranslationService.cpp

HEADERS += $$PWD/../../utils/translation/TranslationService.h
