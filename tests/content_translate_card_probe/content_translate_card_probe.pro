# 在线资源详情页右下角悬浮翻译卡片（QWidget#contentTranslateCard）渲染探针
# 独立于应用主体：不链接 ThemeManager，自己解析 styles/style.qss 的翻译卡片样式块 +
# 从 utils/ThemeManager.cpp 取真实 token，浅色主题渲染并做像素断言。
# 用法：在同一目录建 build/ 后：
#   qmake ../content_translate_card_probe.pro CONFIG+=release && mingw32-make -j4
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = content_translate_card_probe

INCLUDEPATH += $$PWD/../..

SOURCES += content_translate_card_probe.cpp
