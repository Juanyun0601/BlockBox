# 左下角提示卡定位探针：验证提示卡贴窗口左下角、不超出窗口。
# 用法：qmake notification_card_probe.pro && mingw32-make && ./release/notification_card_probe.exe
QT       += core gui widgets svg

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = notification_card_probe

# fake 目录在前，让 #include "utils/ThemeManager.h" 命中探针桩
INCLUDEPATH += $$PWD/fake $$PWD/../..

SOURCES += notification_card_probe.cpp \
           ../../components/NotificationManager.cpp \
           ../../components/NotificationCard.cpp \
           ../../utils/IconHelper.cpp \
           ../../platform.cpp

HEADERS += ../../components/NotificationManager.h \
           ../../components/NotificationCard.h \
           ../../utils/IconHelper.h \
           ../../platform.h \
           fake/utils/ThemeManager.h
