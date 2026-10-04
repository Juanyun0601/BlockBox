#!/bin/bash
# 增量构建 v2：输出到 release_plugin/，避开 Qt Creator 锁定的生成文件
set -u
cd "D:/BlockBox/C-BlockBox" || exit 1

OUT="release_plugin"
mkdir -p "$OUT"

CXX="D:/APP/Qt/Tools/mingw1310_64/bin/g++.exe"
MOC="D:/APP/Qt/6.11.1/mingw_64/bin/moc.exe"
RCC="D:/APP/Qt/6.11.1/mingw_64/bin/rcc.exe"
QT="D:/APP/Qt/6.11.1/mingw_64"
GCC_INC="-ID:/APP/Qt/Tools/mingw1310_64/lib/gcc/x86_64-w64-mingw32/13.1.0/include/c++ -ID:/APP/Qt/Tools/mingw1310_64/lib/gcc/x86_64-w64-mingw32/13.1.0/include/c++/x86_64-w64-mingw32 -ID:/APP/Qt/Tools/mingw1310_64/lib/gcc/x86_64-w64-mingw32/13.1.0/include/c++/backward -ID:/APP/Qt/Tools/mingw1310_64/lib/gcc/x86_64-w64-mingw32/13.1.0/include -ID:/APP/Qt/Tools/mingw1310_64/lib/gcc/x86_64-w64-mingw32/13.1.0/include-fixed -ID:/APP/Qt/Tools/mingw1310_64/x86_64-w64-mingw32/include"

DEF="-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN"
CXXFLAGS="-fno-keep-inline-dllexport -O2 -Wall -Wextra -fexceptions -mthreads"
INC="-I. -Istyles -I$QT/include -I$QT/include/QtSvg -I$QT/include/QtOpenGLWidgets -I$QT/include/QtOpenGL -I$QT/include/QtWidgets -I$QT/include/QtGui -I$QT/include/QtNetwork -I$QT/include/QtConcurrent -I$QT/include/QtCore -I$OUT -I$QT/mkspecs/win32-g++"

MOC_INC="-ID:/APP/Qt/6.11.1/mingw_64/mkspecs/win32-g++ -ID:/BlockBox/C-BlockBox -ID:/BlockBox/C-BlockBox/styles -ID:/APP/Qt/6.11.1/mingw_64/include -ID:/APP/Qt/6.11.1/mingw_64/include/QtSvg -ID:/APP/Qt/6.11.1/mingw_64/include/QtOpenGLWidgets -ID:/APP/Qt/6.11.1/mingw_64/include/QtOpenGL -ID:/APP/Qt/6.11.1/mingw_64/include/QtWidgets -ID:/APP/Qt/6.11.1/mingw_64/include/QtGui -ID:/APP/Qt/6.11.1/mingw_64/include/QtNetwork -ID:/APP/Qt/6.11.1/mingw_64/include/QtConcurrent -ID:/APP/Qt/6.11.1/mingw_64/include/QtCore $GCC_INC"

LIBS="-lz -lpdh -liphlpapi -ldwmapi -L$QT/lib -lQt6Svg -lQt6OpenGLWidgets -lQt6OpenGL -lQt6Widgets -lQt6Gui -lQt6Network -lQt6Concurrent -lQt6Core -lmingw32 -lQt6EntryPoint -lshell32"

fail() { echo "[BUILD-FAIL] $1"; exit 1; }

echo "========== 0. 复制未改动对象 =========="
# 复制 release/*.o 到 release_plugin/（排除将被重新生成的）
for f in release/*.o; do
    b=$(basename "$f")
    case "$b" in
        SideBar.o|mainwindow.o|MainWindowPages.o|moc_mainwindow.o|qrc_resources.o) continue ;;
    esac
    cp -f "$f" "$OUT/$b" 2>/dev/null || echo "  [skip] $b"
done
echo "  copied $(ls "$OUT"/*.o 2>/dev/null | wc -l) objects"

echo "========== 1. rcc 资源 =========="
"$RCC" -name resources --no-zstd resources.qrc -o "$OUT/qrc_resources.cpp" || fail "rcc"
"$CXX" -c "$OUT/qrc_resources.cpp" -o "$OUT/qrc_resources.o" $CXXFLAGS $DEF $INC || fail "rcc compile"

echo "========== 2. moc 生成 =========="
run_moc() {
    local hdr="$1" out="$2"
    echo "  moc $hdr"
    "$MOC" $DEF --include D:/BlockBox/C-BlockBox/release/moc_predefs.h $MOC_INC "$hdr" -o "$OUT/$out" || fail "moc $hdr"
    "$CXX" -c "$OUT/$out" -o "$OUT/${out%.cpp}.o" $CXXFLAGS $DEF $INC || fail "moc compile $out"
}
run_moc mainwindow.h moc_mainwindow.cpp
run_moc utils/plugin/PluginManager.h moc_PluginManager.cpp
run_moc components/CreatePluginDialog.h moc_CreatePluginDialog.cpp
run_moc components/PluginSettingsDialog.h moc_PluginSettingsDialog.cpp
run_moc pages/PluginPage.h moc_PluginPage.cpp
run_moc pages/HomePage.h moc_HomePage.cpp

echo "========== 3. 源文件编译 =========="
compile() {
    echo "  compile $1"
    local src="$1" base
    base=$(basename "$src" .cpp)
    "$CXX" -c "$src" -o "$OUT/$base.o" $CXXFLAGS $DEF $INC || fail "compile $src"
}
compile utils/plugin/PluginZip.cpp
compile utils/plugin/PluginManager.cpp
compile utils/plugin/PluginSafetyGuard.cpp
compile components/CreatePluginDialog.cpp
compile components/PluginSettingsDialog.cpp
compile components/SubNavPanel.cpp
compile pages/PluginPage.cpp
compile pages/HomePage.cpp
compile components/SideBar.cpp
compile mainwindow.cpp
compile MainWindowPages.cpp

echo "========== 4. 链接 =========="
OBJS=$(ls "$OUT"/*.o | tr '\n' ' ')
"$CXX" -o "$OUT/BlockBoxWindows.exe" $OBJS $LIBS -mthreads 2>&1 | tail -40 || fail "link"
echo "[BUILD-DONE] $OUT/BlockBoxWindows.exe"
ls -la "$OUT/BlockBoxWindows.exe"
