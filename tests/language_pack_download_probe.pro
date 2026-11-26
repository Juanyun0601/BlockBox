# 语言包按需下载自测
# 用法（在 tests 目录下）：
#   python -m http.server 18765 --directory ../translations
#   set BLOCKBOX_LANGUAGE_PACK_BASE_URL=http://127.0.0.1:18765/
#   qmake language_pack_download_probe.pro && mingw32-make && .\release\language_pack_download_probe.exe
QT       += core gui widgets network concurrent

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = language_pack_download_probe

INCLUDEPATH += ..

SOURCES += language_pack_download_probe.cpp \
           ../utils/LanguageManager.cpp

HEADERS += ../utils/LanguageManager.h \
           ../utils/AssetUrls.h \
           ../utils/AppVersion.h
