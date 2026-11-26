# 聊天 P2P 直传协议探针的独立工程：只拉 ChatP2P + DownloadTaskManager，
# 不依赖论坛 / 界面，方便本机两个端点互传验证协议。
# 构建：
#   mkdir build && cd build
#   qmake ../chat_p2p_probe.pro && mingw32-make
QT += core network
QT -= gui
TEMPLATE = app
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = chat_p2p_probe

ROOT = ..
INCLUDEPATH += $$ROOT

HEADERS += \
    $$ROOT/utils/ChatP2P.h \
    $$ROOT/utils/DownloadTaskManager.h

SOURCES += \
    chat_p2p_probe.cpp \
    $$ROOT/utils/ChatP2P.cpp \
    $$ROOT/utils/DownloadTaskManager.cpp
