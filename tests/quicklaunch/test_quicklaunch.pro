# 桌面快捷启动（.blockbox）功能独立验证程序
# 用法：qmake test_quicklaunch.pro && mingw32-make && ./test_quicklaunch.exe
# 链接真实的 utils/QuickLaunchManager.cpp，SettingsManager 以伪实现替换。
QT       += core gui network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app

INCLUDEPATH += ../..

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    fake_settingsmanager.cpp \
    ../../utils/QuickLaunchManager.cpp

HEADERS += \
    ../../utils/QuickLaunchManager.h \
    ../../utils/SettingsManager.h

win32: LIBS += -ladvapi32 -lshell32 -lgdi32 -luser32
