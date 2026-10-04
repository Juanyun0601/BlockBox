#!/bin/bash
# 构建并运行自定义背景回归探针（链接主程序全部目标文件，仅替换 main.cpp）
set -e
BUILDDIR="D:/BlockBox/C-BlockBox/build/Desktop_Qt_6_11_1_MinGW_64_bit-Debug"
QTBIN="/d/APP/Qt/6.11.1/mingw_64/bin"
MINGWBIN="/d/APP/Qt/Tools/mingw1310_64/bin"
export PATH="$MINGWBIN:$QTBIN:$PATH"

cd "$BUILDDIR"

# 从主程序链接脚本中剔除 main.o（探针自带 main()）
grep -v "^debug/main\.o$" debug/object_script.BlockBoxWindows.Debug > debug/object_script.Probe.Debug

# 从 Makefile.Debug 提取 LIBS（随 .pro 演进自动跟随，如 Qt6Multimedia）
LIBS_LINE=$(grep -m1 "^LIBS" Makefile.Debug | sed 's/^LIBS[[:space:]]*=[[:space:]]*//')

# 用与 Makefile.Debug 相同的编译参数编译探针
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
    -o debug/probe_main.o ../../tests/background_probe_main.cpp

g++ -Wl,-subsystem,console -mthreads -o probe_out/BlockBoxProbe.exe \
    @debug/object_script.Probe.Debug debug/probe_main.o \
    $LIBS_LINE

echo "=== probe built, running ==="
mkdir -p probe_out
cd probe_out
./BlockBoxProbe.exe 2>&1 | grep -v "^propagateSizeHints\|QWindowsWindow\|libpng warning"
exit ${PIPESTATUS[0]}
