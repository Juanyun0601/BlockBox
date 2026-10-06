# 安装新实例页视觉探针：渲染真实 InstallInstancePage + 真实 style.qss
# 用法：qmake install_page_probe.pro && mingw32-make && ./install_page_probe.exe
QT       += core gui widgets svg concurrent network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = install_page_probe

# fake 目录在前，让 #include "utils/ThemeManager.h" 命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../..

SOURCES += install_page_probe.cpp \
           ../../pages/InstallInstancePage.cpp \
           ../../components/MasonryContentCard.cpp \
           ../../components/ContentViewSwitch.cpp \
           ../../components/BlurLoadingOverlay.cpp \
           ../../components/NotificationManager.cpp \
           ../../components/NotificationCard.cpp \
           ../../layouts/MasonryLayout.cpp \
           ../../layouts/FlowLayout.cpp \
           ../../utils/IconHelper.cpp \
           ../../utils/SettingsManager.cpp \
           ../../platform.cpp

HEADERS += ../../pages/InstallInstancePage.h \
           ../../components/MasonryContentCard.h \
           ../../components/ContentViewSwitch.h \
           ../../components/BlurLoadingOverlay.h \
           ../../components/NotificationManager.h \
           ../../components/NotificationCard.h \
           ../../layouts/MasonryLayout.h \
           ../../layouts/FlowLayout.h \
           ../../utils/IconHelper.h \
           ../../utils/SettingsManager.h \
           ../../platform.h \
           fake/utils/ThemeManager.h

RESOURCES += ../../resources.qrc
