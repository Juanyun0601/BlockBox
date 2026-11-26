#!/bin/bash
# 构建并运行首页封面图空态探针（链接主程序全部目标文件，仅替换 main.cpp）
set -e
BUILDDIR="D:/BlockBox/C-BlockBox/build-win-release"
QTBIN="/d/APP/Qt/6.11.1/mingw_64/bin"
MINGWBIN="/d/APP/Qt/Tools/mingw1310_64/bin"
export PATH="$MINGWBIN:$QTBIN:$PATH"

cd "$BUILDDIR"

# 探针自带 main()：从主程序目标文件列表里剔除 main.o
grep -v "^release/main\.o$" release/object_script.BlockBoxWindows.Release \
    > release/object_script.HomeCoversProbe.Release

LIBS="-lz -lpdh -liphlpapi -ldwmapi -lgdi32 -lole32 -lshell32 -lshlwapi -luuid \
-lcomdlg32 -luser32 -ladvapi32 -lws2_32 -loleaut32 \
-L/d/APP/Qt/6.11.1/mingw_64/lib -lQt6Svg -lQt6OpenGLWidgets -lQt6OpenGL -lQt6Widgets \
-lQt6Gui -lQt6Network -lQt6Concurrent -lQt6Core -lmingw32 -lshell32"

g++ -c -fno-keep-inline-dllexport -O2 -Wall -Wextra -fexceptions -mthreads \
    -DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS \
    -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG \
    -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB \
    -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN \
    -I.. -I../styles -I../third_party/cubiomes \
    -I/d/APP/Qt/6.11.1/mingw_64/include -I/d/APP/Qt/6.11.1/mingw_64/include/QtSvg \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtOpenGLWidgets -I/d/APP/Qt/6.11.1/mingw_64/include/QtOpenGL \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtWidgets -I/d/APP/Qt/6.11.1/mingw_64/include/QtGui \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtNetwork -I/d/APP/Qt/6.11.1/mingw_64/include/QtConcurrent \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtCore -Irelease -I. -I/d/APP/Qt/6.11.1/mingw_64/mkspecs/win32-g++ \
    -o release/home_covers_probe.o ../tests/home_covers_probe.cpp

g++ -mconsole -o release/home_covers_probe.exe release/home_covers_probe.o \
    @release/object_script.HomeCoversProbe.Release \
    $LIBS

echo "[BUILD-DONE] release/home_covers_probe.exe"
cd release
./home_covers_probe.exe
echo "[RUN-DONE]"
