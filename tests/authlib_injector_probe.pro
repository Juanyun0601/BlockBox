# authlib-injector 注入模块探针：真实下载 + JVM 参数拼装
# 用法：qmake authlib_injector_probe.pro && mingw32-make && ./authlib_injector_probe.exe
QT       += core network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = authlib_injector_probe

ROOT      = ..

INCLUDEPATH += $$ROOT

SOURCES += authlib_injector_probe.cpp \
    $$ROOT/utils/auth/AuthlibInjector.cpp

HEADERS += \
    $$ROOT/utils/auth/AuthlibInjector.h

MOC_DIR     = .moc
OBJECTS_DIR = .obj
