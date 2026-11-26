# 数据包管理探针：只编译数据层（不依赖界面），验证 level.dat 读写、
# 世界数据包扫描/预览/兼容性/冲突与导入删除。
# 用法（在任意空目录下）：
#   qmake <本文件路径> && mingw32-make -j8 && ./release/datapack_probe.exe
QT += core
QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = datapack_probe

INCLUDEPATH += ../..

SOURCES += datapack_probe.cpp \
    ../../utils/Saves/DataPackManager.cpp \
    ../../utils/Saves/LevelDatEditor.cpp \
    ../../utils/JarUtils.cpp \
    ../../utils/compat/LocalCompatibilityChecker.cpp

HEADERS += \
    ../../utils/Saves/DataPackManager.h \
    ../../utils/Saves/LevelDatEditor.h \
    ../../utils/JarUtils.h \
    ../../utils/compat/LocalCompatibilityChecker.h

LIBS += -lz
