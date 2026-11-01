#!/bin/bash
# 新手引导「从启动器导入」页交互探针（Git Bash）
# 注意：会写真实的 BlockBox 设置，请通过 run_onboarding_import_probe.ps1 调用（它负责备份/恢复注册表）。
set -e
BUILDDIR="/d/BlockBox/C-BlockBox/build/Desktop_Qt_6_11_1_MinGW_64_bit-Debug"
export PATH="/d/APP/Qt/Tools/mingw1310_64/bin:/d/APP/Qt/6.11.1/mingw_64/bin:$PATH"
export TMP=/d/tmp/gtmp
export TEMP=/d/tmp/gtmp
mkdir -p "$TMP"
mkdir -p "$BUILDDIR/probe_out"
mkdir -p /d/BlockBox/_out/onboarding_probe

cd "$BUILDDIR"

echo "=== rebuild objects ==="
mingw32-make -f Makefile.Debug debug/OnboardingWizard.o 2>&1 | tail -10

echo "=== compile probe ==="
g++ -c -fno-keep-inline-dllexport -g -Wall -fexceptions -mthreads \
    -DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS \
    -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_QML_DEBUG \
    -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB \
    -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN \
    -I../../../C-BlockBox -I. -I../../styles \
    -I/d/APP/Qt/6.11.1/mingw_64/include \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtSvg \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtOpenGLWidgets \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtOpenGL \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtWidgets \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtGui \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtNetwork \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtConcurrent \
    -I/d/APP/Qt/6.11.1/mingw_64/include/QtCore \
    -Idebug -I. -I/d/APP/Qt/6.11.1/mingw_64/mkspecs/win32-g++ \
    -o debug/onboarding_import_probe.o \
    "D:/BlockBox/C-BlockBox/tests/onboarding_import_probe.cpp"

echo "=== link ==="
grep -v "^debug/main\.o$" debug/object_script.BlockBoxWindows.Debug > debug/object_script.OnboardingImportProbe.Debug
LIBS_LINE=$(grep -m1 "^LIBS" Makefile.Debug | sed 's/^LIBS[[:space:]]*=[[:space:]]*//' | tr '\\' '/')

g++ -Wl,-subsystem,console -mthreads -o probe_out/OnboardingImportProbe.exe \
    @debug/object_script.OnboardingImportProbe.Debug \
    debug/onboarding_import_probe.o \
    $LIBS_LINE

echo "=== run ==="
cd probe_out
./OnboardingImportProbe.exe 2>&1 | grep -v "libpng warning\|propagateSizeHints" || true

echo "=== screenshots ==="
ls -la /d/BlockBox/_out/onboarding_probe/ | tail -8
