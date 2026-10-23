# 安装流程探针：真实 LoaderDetailPage + 真实 VersionDownloader，模拟用户点“安装”按钮
# 校验：版本文件必须落到 <游戏根>/versions/<实例名>/ 下，且 installRequested 携带该目录
# 用法：qmake install_flow_probe.pro && mingw32-make -j8
#       ./install_flow_probe.exe [版本id] [游戏根目录] [实例名]
# 退出码：0=通过 1=安装失败 2=超时 3=产物缺失 4=控件缺失 5=路径未落位
QT       += core gui widgets svg concurrent network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = install_flow_probe

# fake 目录在前：加载器安装器与 ThemeManager 走探针桩，VersionDownloader 用真实实现
INCLUDEPATH += $$PWD/fake $$PWD/../..

SOURCES += install_flow_probe.cpp \
           ../../pages/LoaderDetailPage.cpp \
           ../../components/MasonryContentCard.cpp \
           ../../components/CustomCheckBox.cpp \
           ../../components/NotificationManager.cpp \
           ../../components/NotificationCard.cpp \
           ../../utils/IconHelper.cpp \
           ../../utils/LoaderCompatibility.cpp \
           ../../utils/SettingsManager.cpp \
           ../../utils/VersionDownloader.cpp \
           ../../utils/DownloadTaskManager.cpp \
           ../../utils/ManifestCache.cpp \
           ../../utils/FileReuse.cpp \
           ../../platform.cpp

HEADERS += ../../pages/LoaderDetailPage.h \
           ../../components/MasonryContentCard.h \
           ../../components/CustomCheckBox.h \
           ../../components/NotificationManager.h \
           ../../components/NotificationCard.h \
           ../../utils/IconHelper.h \
           ../../utils/LoaderCompatibility.h \
           ../../utils/SettingsManager.h \
           ../../utils/VersionDownloader.h \
           ../../utils/DownloadTaskManager.h \
           ../../utils/ManifestCache.h \
           ../../utils/FileReuse.h \
           ../../platform.h \
           fake/utils/ThemeManager.h \
           fake/utils/forge/ForgeInstaller.h \
           fake/utils/NeoForgeInstaller.h \
           fake/utils/fabric/FabricInstaller.h \
           fake/utils/optifine/OptiFineInstaller.h \
           fake/utils/LiteLoaderInstaller.h \
           fake/utils/CleanroomInstaller.h \
           fake/utils/OptiFabricInstaller.h \
           fake/components/AppFileDialog.h

RESOURCES += ../../resources.qrc
