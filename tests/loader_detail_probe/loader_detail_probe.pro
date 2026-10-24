# 加载器详情页视觉探针：渲染真实 LoaderDetailPage + 真实 style.qss
# 用法：qmake loader_detail_probe.pro && mingw32-make && ./loader_detail_probe.exe
QT       += core gui widgets svg concurrent network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = loader_detail_probe

# fake 目录在前，让 #include "utils/ThemeManager.h" 等命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../..

SOURCES += loader_detail_probe.cpp \
           ../../pages/LoaderDetailPage.cpp \
           ../../components/MasonryContentCard.cpp \
           ../../components/CustomCheckBox.cpp \
           ../../components/NotificationManager.cpp \
           ../../components/NotificationCard.cpp \
           ../../utils/IconHelper.cpp \
           ../../utils/LoaderCompatibility.cpp \
           ../../utils/SettingsManager.cpp \
           ../../platform.cpp

HEADERS += ../../pages/LoaderDetailPage.h \
           ../../components/MasonryContentCard.h \
           ../../components/CustomCheckBox.h \
           ../../components/NotificationManager.h \
           ../../components/NotificationCard.h \
           ../../utils/IconHelper.h \
           ../../utils/LoaderCompatibility.h \
           ../../utils/SettingsManager.h \
           ../../platform.h \
           fake/utils/ThemeManager.h \
           fake/utils/VersionDownloader.h \
           fake/utils/forge/ForgeInstaller.h \
           fake/utils/NeoForgeInstaller.h \
           fake/utils/fabric/FabricInstaller.h \
           fake/utils/optifine/OptiFineInstaller.h \
           fake/utils/LiteLoaderInstaller.h \
           fake/utils/CleanroomInstaller.h \
           fake/utils/OptiFabricInstaller.h \
           fake/components/AppFileDialog.h

RESOURCES += ../../resources.qrc ../../resources_data.qrc
