# 在线整合包安装链路探针：真实下载包 → 识别格式 → 解析清单 → 换取模组下载地址
# 用法：qmake online_modpack_probe.pro && mingw32-make && ./online_modpack_probe.exe
QT       += core network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET    = online_modpack_probe

INCLUDEPATH += ..

SOURCES += \
    online_modpack_probe.cpp \
    ../utils/modpack/ModpackDetector.cpp

HEADERS += \
    ../utils/modpack/ModpackDetector.h \
    ../utils/modpack/ModpackInfo.h

LIBS += -lz
