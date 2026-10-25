TARGET = neoforge_e2e_probe
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
QT = core network concurrent

ROOT = ../..

SOURCES += \
    neoforge_e2e_probe.cpp \
    $$ROOT/platform.cpp \
    $$ROOT/utils/NeoForgeInstaller.cpp \
    $$ROOT/utils/forge/ForgeDownloader.cpp \
    $$ROOT/utils/forge/ForgeNewInstallTask.cpp \
    $$ROOT/utils/forge/ForgeProfile.cpp \
    $$ROOT/utils/forge/LoaderVersionDir.cpp \
    $$ROOT/utils/net/Download.cpp \
    $$ROOT/utils/net/FileSink.cpp \
    $$ROOT/utils/net/HttpMetaCache.cpp \
    $$ROOT/utils/net/MetaCacheSink.cpp \
    $$ROOT/utils/net/NetJob.cpp \
    $$ROOT/utils/net/Task.cpp \
    $$ROOT/utils/JarUtils.cpp \
    $$ROOT/utils/DownloadTaskManager.cpp \
    $$ROOT/utils/ManifestCache.cpp \
    $$ROOT/utils/SettingsManager.cpp \
    $$ROOT/utils/MemoryAllocator.cpp \
    $$ROOT/utils/translation/TranslationService.cpp

HEADERS += \
    $$ROOT/platform.h \
    $$ROOT/utils/NeoForgeInstaller.h \
    $$ROOT/utils/forge/ForgeDownloader.h \
    $$ROOT/utils/forge/ForgeNewInstallTask.h \
    $$ROOT/utils/forge/ForgeProfile.h \
    $$ROOT/utils/forge/LoaderVersionDir.h \
    $$ROOT/utils/JarUtils.h \
    $$ROOT/utils/Safety.h \
    $$ROOT/utils/DownloadTaskManager.h \
    $$ROOT/utils/ManifestCache.h \
    $$ROOT/utils/SettingsManager.h \
    $$ROOT/utils/MemoryAllocator.h \
    $$ROOT/utils/translation/TranslationService.h \
    $$ROOT/utils/net/Download.h \
    $$ROOT/utils/net/FileSink.h \
    $$ROOT/utils/net/HttpMetaCache.h \
    $$ROOT/utils/net/MetaCacheSink.h \
    $$ROOT/utils/net/NetAction.h \
    $$ROOT/utils/net/NetJob.h \
    $$ROOT/utils/net/Task.h

INCLUDEPATH += $$ROOT $$ROOT/utils
DEFINES += WINDOWS_OS


