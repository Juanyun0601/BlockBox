# 详情页「前置资源」数据链路探针（真实网络）
# 用法：qmake detail_dependency_probe.pro && mingw32-make release && release/detail_dependency_probe.exe
QT       += core gui network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = detail_dependency_probe

INCLUDEPATH += $$PWD/../..

SOURCES += detail_dependency_probe.cpp \
           ../../utils/mod/ModrinthAPI.cpp \
           ../../utils/mod/CurseForgeAPI.cpp \
           ../../utils/mod/ModData.cpp \
           ../../utils/SettingsManager.cpp \
           ../../utils/translation/TranslationService.cpp \
           ../../platform.cpp

HEADERS += ../../utils/mod/ModrinthAPI.h \
           ../../utils/mod/CurseForgeAPI.h \
           ../../utils/mod/ModData.h \
           ../../utils/SettingsManager.h \
           ../../utils/translation/TranslationService.h \
           ../../platform.h
