TARGET = vdtest
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
QT = core network

ROOT = ../..

SOURCES += \
    main.cpp \
    $$ROOT/platform.cpp \
    $$ROOT/utils/VersionDownloader.cpp \
    $$ROOT/utils/DownloadTaskManager.cpp \
    $$ROOT/utils/ManifestCache.cpp \
    $$ROOT/utils/SettingsManager.cpp \
    $$ROOT/utils/translation/TranslationService.cpp \
    $$ROOT/utils/MemoryAllocator.cpp

HEADERS += \
    $$ROOT/platform.h \
    $$ROOT/utils/VersionDownloader.h \
    $$ROOT/utils/DownloadTaskManager.h \
    $$ROOT/utils/ManifestCache.h \
    $$ROOT/utils/SettingsManager.h \
    $$ROOT/utils/translation/TranslationService.h \
    $$ROOT/utils/MemoryAllocator.h

INCLUDEPATH += $$ROOT $$ROOT/utils
DEFINES += WINDOWS_OS
