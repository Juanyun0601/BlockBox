# 共享翻译切换卡片组件（components/TranslateSwitchCard）行为 + 渲染探针
# 直接链接真实组件源码（不再镜像实现），解析 styles/style.qss 的真实样式块做像素断言，
# 并覆盖「中 / EN 切换、busy 态、选中态」等交互，以及列表页批量翻译结果的解析规则。
# 用法：在同一目录建 build/ 后：
#   qmake ../translate_switch_card_probe.pro CONFIG+=release && mingw32-make -j4
QT       += core gui widgets

CONFIG   += c++17 console
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = translate_switch_card_probe

INCLUDEPATH += $$PWD/../..

SOURCES += translate_switch_card_probe.cpp \
           $$PWD/../../components/TranslateSwitchCard.cpp

HEADERS += $$PWD/../../components/TranslateSwitchCard.h
