# 实例管理页本地资源多维筛选行为探针：真实 InstanceFilePage + 真实样式无关（纯行为断言）
# 用法：qmake file_filter_probe.pro && mingw32-make release && release/file_filter_probe.exe
QT       += core gui widgets svg network concurrent

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = file_filter_probe

# fake 目录在前：#include "utils/ThemeManager.h" 命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../..

SOURCES += file_filter_probe.cpp \
           ../../pages/InstanceFilePage.cpp \
           ../../pages/ResourcesPage.cpp \
           ../../components/BlurLoadingOverlay.cpp \
           ../../components/FavoriteFolderDialog.cpp \
           ../../components/LocalCategoryDialog.cpp \
           ../../components/MasonryContentCard.cpp \
           ../../components/AppMessageBox.cpp \
           ../../components/AppDialogBase.cpp \
           ../../components/ContentViewSwitch.cpp \
           ../../components/OutlinedLabel.cpp \
           ../../layouts/MasonryLayout.cpp \
           ../../layouts/FlowLayout.cpp \
           ../../utils/IconHelper.cpp \
           ../../utils/LocalCategoryManager.cpp \
           ../../utils/ThumbnailProvider.cpp \
           ../../utils/mod/ModrinthAPI.cpp \
           ../../utils/mod/ModData.cpp \
           ../../utils/content/ContentDownloader.cpp \
           ../../utils/DownloadTaskManager.cpp \
           ../../utils/FavoritesManager.cpp \
           ../../utils/JarUtils.cpp \
           ../../utils/SettingsManager.cpp \
           ../../utils/LanguageManager.cpp \
           ../../utils/UpdateChecker.cpp \
           ../../utils/AndroidBridge.cpp \
           ../../utils/HarmonyBridge.cpp \
           ../../utils/GithubAccelerator.cpp \
           ../../platform.cpp

HEADERS += ../../pages/InstanceFilePage.h \
           ../../pages/ResourcesPage.h \
           ../../components/BlurLoadingOverlay.h \
           ../../components/FavoriteFolderDialog.h \
           ../../components/LocalCategoryDialog.h \
           ../../components/MasonryContentCard.h \
           ../../components/AppMessageBox.h \
           ../../components/AppDialogBase.h \
           ../../components/ContentViewSwitch.h \
           ../../components/OutlinedLabel.h \
           ../../layouts/MasonryLayout.h \
           ../../layouts/FlowLayout.h \
           ../../utils/IconHelper.h \
           ../../utils/LocalCategoryManager.h \
           ../../utils/ThumbnailProvider.h \
           ../../utils/mod/ModrinthAPI.h \
           ../../utils/mod/ModData.h \
           ../../utils/content/ContentDownloader.h \
           ../../utils/DownloadTaskManager.h \
           ../../utils/FavoritesManager.h \
           ../../utils/JarUtils.h \
           ../../utils/SettingsManager.h \
           ../../utils/LanguageManager.h \
           ../../utils/UpdateChecker.h \
           ../../utils/AndroidBridge.h \
           ../../utils/HarmonyBridge.h \
           ../../utils/GithubAccelerator.h \
           fake/utils/ThemeManager.h

RESOURCES += ../../resources.qrc
LIBS    += -lgdi32 -lole32 -lshell32 -lshlwapi -luser32 -ladvapi32 -lws2_32 -loleaut32 -luuid -lcomdlg32
