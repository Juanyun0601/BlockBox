# 跨源合并端到端联调探针（真实网络）：API 搜索 → mergeCrossSourceResults
# 用法：qmake live_merge_probe.pro && mingw32-make release && release/live_merge_probe.exe [关键词]
QT       += core network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = live_merge_probe

INCLUDEPATH += $$PWD/..

SOURCES += live_merge_probe.cpp \
           ../utils/mod/ModrinthAPI.cpp \
           ../utils/mod/CurseForgeAPI.cpp \
           ../utils/mod/ModData.cpp \
           ../utils/SettingsManager.cpp \
           ../utils/translation/TranslationService.cpp \
           ../platform.cpp

HEADERS += ../utils/mod/ModrinthAPI.h \
           ../utils/mod/CurseForgeAPI.h \
           ../utils/mod/ModData.h \
           ../utils/SettingsManager.h \
           ../utils/translation/TranslationService.h \
           ../platform.h
