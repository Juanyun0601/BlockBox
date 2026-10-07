# 统一快速检测探针（JavaScanWorker::runQuickScan，自包含编译）
QT       += core
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = java_detect_quick_probe

INCLUDEPATH += ..
HEADERS  += ../utils/JavaScanWorker.h
SOURCES  += java_detect_quick_probe.cpp \
            ../utils/JavaScanWorker.cpp
