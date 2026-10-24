#!/bin/bash
# 详情页版本卡片「下载」图标可读性探针 —— 构建脚本
#
#   bash build.sh          # AFTER：编译当前 ContentDetailPage.cpp（修复后）
#   bash build.sh before   # BEFORE：复用对象目录里的旧 ContentDetailPage.o（修复前）
#
# 做法同 detail_download_filter_probe：复用全量构建的对象目录，只重编探针与被测页面。
set -e

CD=/d/BlockBox/C-BlockBox
OBJ=$CD/build-win-release/release
MODE=${1:-after}
OUT=$CD/tests/detail_download_icon_probe/build_$MODE

CXX=/d/APP/Qt/Tools/mingw1310_64/bin/g++.exe
QT=/d/APP/Qt/6.11.1/mingw_64
QTW=D:/APP/Qt/6.11.1/mingw_64
NM=/d/APP/Qt/Tools/mingw1310_64/bin/nm.exe

DEF="-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB"
INC="-I$CD -I$CD/styles -I$QT/include -I$QT/include/QtSvg -I$QT/include/QtOpenGLWidgets -I$QT/include/QtOpenGL -I$QT/include/QtWidgets -I$QT/include/QtGui -I$QT/include/QtNetwork -I$QT/include/QtConcurrent -I$QT/include/QtCore -I$QT/mkspecs/win32-g++"

if [ ! -d "$OBJ" ]; then
    echo "对象目录不存在：$OBJ（先做一次全量构建）" >&2
    exit 1
fi

mkdir -p "$OUT"

echo "==== 1. 编译探针 ===="
"$CXX" -c "$CD/tests/detail_download_icon_probe/probe.cpp" -o "$OUT/probe.o" \
    -O1 -fexceptions -mthreads $DEF $INC

if [ "$MODE" = "before" ]; then
    echo "==== 2. 复用旧 ContentDetailPage.o（修复前） ===="
    PAGE_OBJ=$(cygpath -m "$OBJ/ContentDetailPage.o")
else
    echo "==== 2. 编译当前 ContentDetailPage.cpp（修复后） ===="
    "$CXX" -c "$CD/pages/ContentDetailPage.cpp" -o "$OUT/ContentDetailPage_probe.o" \
        -O1 -fexceptions -mthreads $DEF $INC
    PAGE_OBJ=$(cygpath -m "$OUT/ContentDetailPage_probe.o")
fi

echo "==== 3. 生成链接响应文件 ===="
OUTW=$(cygpath -m "$OUT")
OBJW=$(cygpath -m "$OBJ")
RSP=$OUT/link.rsp
: > "$RSP"
echo "$OUTW/probe.o" >> "$RSP"
echo "$PAGE_OBJ" >> "$RSP"
for f in "$OBJW"/*.o; do
    base=$(basename "$f")
    case "$base" in
        main.o|ContentDetailPage.o) continue ;;
    esac
    if "$NM" --defined-only "$f" 2>/dev/null | grep -qE ' T (_Z5qMain|_?main$)'; then
        continue
    fi
    echo "$f" >> "$RSP"
done

{
    echo "-L$QTW/lib"
    echo "-lQt6Svg -lQt6OpenGLWidgets -lQt6OpenGL -lQt6Widgets -lQt6Gui -lQt6Network -lQt6Concurrent -lQt6Core"
    echo "-lz -lpdh -liphlpapi -ldwmapi -lgdi32 -lole32 -lshell32 -lshlwapi -luuid -lcomdlg32 -luser32 -ladvapi32 -lws2_32 -loleaut32 -lmingw32"
} >> "$RSP"

echo "==== 4. 链接 ===="
"$CXX" @"$RSP" -o "$OUT/probe.exe" -mthreads -Wl,--subsystem,console

echo "==== 构建完成：$OUT/probe.exe ===="
