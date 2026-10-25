# 全盘扫描探针（JavaScanWorker::doScan 快速检测 + 全盘深扫，自包含编译）
QT       += core
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = java_full_scan_probe

INCLUDEPATH += ..
HEADERS  += ../utils/JavaScanWorker.h
SOURCES  += java_full_scan_probe.cpp \
            ../utils/JavaScanWorker.cpp
