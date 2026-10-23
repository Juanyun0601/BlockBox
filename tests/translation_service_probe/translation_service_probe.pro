# 翻译服务探针：用本地假服务端验证「请求地址 / 请求头 / 请求体 / 错误提示」的真实行为
# 直接链接 utils/translation/TranslationService.cpp（真实实现），不联网。
# 用法：在本目录建 build/ 后：
#   qmake ../translation_service_probe.pro CONFIG+=release && mingw32-make -j4
QT       += core network

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = translation_service_probe

INCLUDEPATH += $$PWD/../..

SOURCES += translation_service_probe.cpp \
           $$PWD/../../utils/translation/TranslationService.cpp

HEADERS += $$PWD/../../utils/translation/TranslationService.h
