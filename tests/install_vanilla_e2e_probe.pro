# 安装新实例端到端探针：真实 VersionDownloader::downloadVanillaTo 全链路
# 用法：
#   qmake install_vanilla_e2e_probe.pro && mingw32-make -j8
#   ./install_vanilla_e2e_probe.exe [版本id] [official|bmcl] [目标目录]
# 退出码：0=安装成功 1=downloadFailed 2=超时卡死 3=完成但产物缺失
QT       += core network gui widgets
CONFIG   += c++17 console
CONFIG   -= app_bundle
TEMPLATE  = app
TARGET    = install_vanilla_e2e_probe

INCLUDEPATH += ..

SOURCES  += install_vanilla_e2e_probe.cpp \
            ../utils/VersionDownloader.cpp \
            ../utils/DownloadTaskManager.cpp \
            ../utils/SettingsManager.cpp \
            ../utils/ManifestCache.cpp \
            ../utils/FileReuse.cpp \
            ../platform.cpp

HEADERS  += ../utils/VersionDownloader.h \
            ../utils/DownloadTaskManager.h \
            ../utils/SettingsManager.h \
            ../utils/ManifestCache.h \
            ../utils/FileReuse.h
