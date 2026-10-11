# 瀑布流卡片徽标视觉探针：无封面 → 徽标封面区居中放大；封面回填 → 收回右下角
# 用法：qmake masonry_badge_probe.pro && mingw32-make release && release/masonry_badge_probe.exe
QT       += core gui widgets svg network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = masonry_badge_probe

# fake 目录在前：#include "utils/ThemeManager.h" 命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../..

SOURCES += masonry_badge_probe.cpp \
           ../../components/MasonryContentCard.cpp \
           ../../utils/IconHelper.cpp \
           ../../utils/SettingsManager.cpp \
           ../../platform.cpp

HEADERS += ../../components/MasonryContentCard.h \
           ../../utils/IconHelper.h \
           ../../utils/SettingsManager.h \
           ../../platform.h \
           fake/utils/ThemeManager.h

RESOURCES += ../../resources.qrc
