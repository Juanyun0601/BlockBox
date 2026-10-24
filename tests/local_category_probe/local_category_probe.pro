# 本地资源「管理分类」对话框视觉探针：渲染真实 LocalCategoryDialog + 真实 style.qss
# 用法：qmake local_category_probe.pro && mingw32-make release && release/local_category_probe.exe
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = local_category_probe

# fake 目录在前，让 #include "components/AppMessageBox.h" 命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../..

SOURCES += local_category_probe.cpp \
           ../../components/LocalCategoryDialog.cpp \
           ../../components/FavoriteFolderDialog.cpp \
           ../../layouts/FlowLayout.cpp \
           ../../utils/LocalCategoryManager.cpp

HEADERS += ../../components/LocalCategoryDialog.h \
           ../../components/FavoriteFolderDialog.h \
           ../../layouts/FlowLayout.h \
           ../../utils/LocalCategoryManager.h \
           fake/components/AppMessageBox.h
