# 在线资源详情页「筛选选项」版本列表探针（真实网络）
# 用法：qmake detail_filter_probe.pro && mingw32-make release && release/detail_filter_probe.exe
QT       += core gui network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = detail_filter_probe

INCLUDEPATH += $$PWD/../..

SOURCES += detail_filter_probe.cpp \
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
