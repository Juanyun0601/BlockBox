#!/bin/bash
# 构建并运行必应壁纸端到端探针（链接主程序全部目标文件，仅替换 main.cpp）
set -e
BUILDDIR="D:/BlockBox/C-BlockBox/build/Desktop_Qt_6_11_1_MinGW_64_bit-Debug"
QTBIN="/d/APP/Qt/6.11.1/mingw_64/bin"
MINGWBIN="/d/APP/Qt/Tools/mingw1310_64/bin"
export PATH="$MINGWBIN:$QTBIN:$PATH"
export TMP="/d/Temp/blockbox_build" TEMP="/d/Temp/blockbox_build" TMPDIR="/d/Temp/blockbox_build"

cd "$BUILDDIR"

grep -v "^debug/main\.o$" debug/object_script.BlockBoxWindows.Debug > debug/object_script.BingProbe.Debug
LIBS_LINE=$(grep -m1 "^LIBS" Makefile.Debug | sed 's/^LIBS[[:space:]]*=[[:space:]]*//')

mkdir -p probe_out
g++ -c -fno-keep-inline-dllexport -g -Wall -Wextra -fexceptions -mthreads \
    -DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS \
    -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_QML_DEBUG \
    -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB \
    -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN \
    -I../../../C-BlockBox -I. -I../../styles \
    -I/d/APP/Qt/6.11.1/mingw_64/include -I/d/APP/Qt/6.11.1/mingw_64/include/QtSvg \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtOpenGLWidgets -I/d/APP/Qt/6.11.1/mingw_64/include/QtOpenGL \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtWidgets -I/d/APP/Qt/6.11.1/mingw_64/include/QtGui \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtNetwork -I/d/APP/Qt/6.11.1/mingw_64/include/QtConcurrent \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtCore -Idebug -I/d/APP/Qt/6.11.1/mingw_64/mkspecs/win32-g++ \
    -o debug/bing_wallpaper_probe.o ../../tests/bing_wallpaper_probe.cpp

g++ -Wl,-subsystem,console -mthreads -o probe_out/BingWallpaperProbe.exe \
    @debug/object_script.BingProbe.Debug debug/bing_wallpaper_probe.o \
    $LIBS_LINE

echo "=== probe built, running ==="
cd probe_out
./BingWallpaperProbe.exe 2>&1 | grep -v "libpng warning\|propagateSizeHints"
exit ${PIPESTATUS[0]}
