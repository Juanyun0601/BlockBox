#!/bin/bash
# 资源详情页「下载列表」实例筛选探针 —— 构建脚本
#
# 做法：复用一次全量构建产出的对象目录（默认 build-win-release/release），
#       只重新编译 pages/ContentDetailPage.cpp 和本探针，避免拉起重型依赖链。
#
# 用法：
#   bash build.sh                       # 用默认对象目录
#   bash build.sh /d/BlockBox/C-BlockBox/build-win-release/release
#   QT_QPA_PLATFORM=offscreen ./probe.exe
set -e

CD=/d/BlockBox/C-BlockBox
OBJ=${1:-$CD/build-win-release/release}
OUT=$CD/tests/detail_download_filter_probe/build

CXX=/d/APP/Qt/Tools/mingw1310_64/bin/g++.exe
QT=/d/APP/Qt/6.11.1/mingw_64
# 链接器（mingw ld）只认 Windows 风格路径，响应文件里统一用 QTW
QTW=D:/APP/Qt/6.11.1/mingw_64

DEF="-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB"
INC="-I$CD -I$CD/styles -I$QT/include -I$QT/include/QtSvg -I$QT/include/QtOpenGLWidgets -I$QT/include/QtOpenGL -I$QT/include/QtWidgets -I$QT/include/QtGui -I$QT/include/QtNetwork -I$QT/include/QtConcurrent -I$QT/include/QtCore -I$QT/mkspecs/win32-g++"

if [ ! -d "$OBJ" ]; then
    echo "对象目录不存在：$OBJ（先做一次全量构建）" >&2
    exit 1
fi

mkdir -p "$OUT"

echo "==== 1. 编译探针 ===="
"$CXX" -c "$CD/tests/detail_download_filter_probe/probe.cpp" -o "$OUT/probe.o" \
    -O1 -fexceptions -mthreads $DEF $INC

echo "==== 2. 编译被测页面（当前源码） ===="
"$CXX" -c "$CD/pages/ContentDetailPage.cpp" -o "$OUT/ContentDetailPage_probe.o" \
    -O1 -fexceptions -mthreads $DEF $INC

echo "==== 3. 生成链接响应文件 ===="
OUTW=$(cygpath -m "$OUT")
OBJW=$(cygpath -m "$OBJ")
NM=/d/APP/Qt/Tools/mingw1310_64/bin/nm.exe
RSP=$OUT/link.rsp
: > "$RSP"
echo "$OUTW/probe.o" >> "$RSP"
echo "$OUTW/ContentDetailPage_probe.o" >> "$RSP"
# 对象目录里混有其它探针的 main/qMain，全部剔除，否则多重定义
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
