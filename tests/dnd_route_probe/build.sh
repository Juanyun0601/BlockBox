#!/bin/bash
# 拖放路由探针：构建并依次跑四个落点场景
#
# 依赖 Qt 6.11.1 mingw_64 + mingw1310_64（与本仓库其它探针一致）。
# 也可以在 PowerShell 里手工编译，命令见 tests/update_install_probe/run_test.ps1。
set -e

CD="D:/BlockBox/C-BlockBox/tests/dnd_route_probe"
OUT="$CD/build"
QT="D:/APP/Qt/6.11.1/mingw_64"
CXX="D:/APP/Qt/Tools/mingw1310_64/bin/g++.exe"

DEF="-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DQT_NO_DEBUG -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB"
INC="-I$QT/include -I$QT/include/QtWidgets -I$QT/include/QtGui -I$QT/include/QtCore -I$QT/mkspecs/win32-g++"

mkdir -p "$OUT"
"$CXX" "$CD/probe.cpp" -o "$OUT/probe.exe" \
    -O1 -fexceptions -mthreads $DEF $INC \
    -L"$QT/lib" -lQt6Widgets -lQt6Gui -lQt6Core \
    -lmingw32 -lole32 -loleaut32 -luuid -lshell32 -luser32 -ladvapi32 -lws2_32 \
    -Wl,--subsystem,console

for scenario in 1 2 3 4; do
    echo "########## 场景 $scenario"
    QT_QPA_PLATFORM=windows "$OUT/probe.exe" "$scenario" || echo "（退出码 $?）"
done
