# 加载器详情页（版本列表）视觉探针：渲染真实 ForgeVersionListPage + 真实 style.qss
# 用法：qmake forge_version_list_probe.pro && mingw32-make && ./forge_version_list_probe.exe
QT       += core gui widgets svg concurrent network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = forge_version_list_probe

# 复用 loader_detail_probe 的 fake 桩目录（fake 在前命中 ThemeManager / 安装器桩）
INCLUDEPATH += $$PWD/../loader_detail_probe/fake $$PWD/../..

SOURCES += forge_version_list_probe.cpp \
           ../../pages/ForgeVersionListPage.cpp \
           ../../components/MasonryContentCard.cpp \
           ../../components/BlurLoadingOverlay.cpp \
           ../../components/NotificationManager.cpp \
           ../../components/NotificationCard.cpp \
           ../../utils/IconHelper.cpp \
           ../../utils/SettingsManager.cpp \
           ../../platform.cpp

HEADERS += ../../pages/ForgeVersionListPage.h \
           ../../components/MasonryContentCard.h \
           ../../components/BlurLoadingOverlay.h \
           ../../components/NotificationManager.h \
           ../../components/NotificationCard.h \
           ../../utils/IconHelper.h \
           ../../utils/SettingsManager.h \
           ../../platform.h \
           ../loader_detail_probe/fake/utils/ThemeManager.h \
           ../loader_detail_probe/fake/utils/forge/ForgeInstaller.h \
           ../loader_detail_probe/fake/utils/optifine/OptiFineInstaller.h \
           ../loader_detail_probe/fake/utils/LiteLoaderInstaller.h \
           ../loader_detail_probe/fake/utils/CleanroomInstaller.h \
           ../loader_detail_probe/fake/utils/OptiFabricInstaller.h

RESOURCES += ../../resources.qrc ../../resources_data.qrc
