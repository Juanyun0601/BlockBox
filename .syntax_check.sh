#!/bin/bash
# 手动编译验证插件功能相关文件（仅语法检查，-c 不链接）
set -u
CXX="D:/APP/Qt/Tools/mingw1310_64/bin/g++.exe"
QT="D:/APP/Qt/6.11.1/mingw_64"
DEF="-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN"
CXXFLAGS="-fno-keep-inline-dllexport -O2 -Wall -Wextra -fexceptions -mthreads"
INC="-I. -Istyles -I$QT/include -I$QT/include/QtSvg -I$QT/include/QtOpenGLWidgets -I$QT/include/QtOpenGL -I$QT/include/QtWidgets -I$QT/include/QtGui -I$QT/include/QtNetwork -I$QT/include/QtConcurrent -I$QT/include/QtCore -I$QT/mkspecs/win32-g++"

cd "D:/BlockBox/C-BlockBox" || exit 1
mkdir -p .syntax_check

FAIL=0
for f in "$@"; do
    base=$(basename "$f" .cpp)
    echo "===== compiling: $f ====="
    if "$CXX" -c "$f" -o ".syntax_check/$base.o" $CXXFLAGS $DEF $INC 2>&1; then
        echo "  [OK] $f"
    else
        echo "  [FAIL] $f"
        FAIL=1
    fi
done
echo "================================"
[ $FAIL -eq 0 ] && echo "ALL OK" || echo "SOME FAILED"
exit $FAIL
