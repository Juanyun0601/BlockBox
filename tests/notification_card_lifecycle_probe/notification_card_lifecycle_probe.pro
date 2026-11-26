# 左下角提示卡生命周期探针：验证倒计时结束后一定消失（退场被重排打断、悬停漏收 leave 也不留残影）。
# 用法：qmake notification_card_lifecycle_probe.pro && mingw32-make && ./release/notification_card_lifecycle_probe.exe
QT       += core gui widgets svg network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = notification_card_lifecycle_probe

# fake 目录在前，让 #include "utils/ThemeManager.h" 命中探针桩
INCLUDEPATH += $$PWD/../notification_card_probe/fake $$PWD/../..

SOURCES += notification_card_lifecycle_probe.cpp \
           ../../components/NotificationManager.cpp \
           ../../components/NotificationCard.cpp \
           ../../components/NetworkCheckDialog.cpp \
           ../../components/AppDialogBase.cpp \
           ../../utils/IconHelper.cpp \
           ../../platform.cpp

HEADERS += ../../components/NotificationManager.h \
           ../../components/NotificationCard.h \
           ../../components/NetworkCheckDialog.h \
           ../../components/AppDialogBase.h \
           ../../utils/IconHelper.h \
           ../../platform.h \
           ../notification_card_probe/fake/utils/ThemeManager.h

LIBS += -ldwmapi
