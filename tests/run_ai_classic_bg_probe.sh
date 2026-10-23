#!/bin/bash
# 构建并运行「AI 页主栏经典模式背景一致性」探针（链接主程序全部目标文件，仅替换 main.cpp）
# 用法:
#   ./run_ai_classic_bg_probe.sh              # 用现有资源（改动前）
#   REGEN_QRC=1 ./run_ai_classic_bg_probe.sh  # 先用 rcc 重新生成 style.qss 资源再链接（改动后）
set -e
BUILDDIR="D:/BlockBox/C-BlockBox/build/Desktop_Qt_6_11_1_MinGW_64_bit-Debug"
QTBIN="/d/APP/Qt/6.11.1/mingw_64/bin"
MINGWBIN="/d/APP/Qt/Tools/mingw1310_64/bin"
export PATH="$MINGWBIN:$QTBIN:$PATH"
export TMP="/d/Temp/blockbox_build" TEMP="/d/Temp/blockbox_build" TMPDIR="/d/Temp/blockbox_build"

cd "$BUILDDIR"

OBJECT_SCRIPT="debug/object_script.AiClassicBgProbe.Debug"
# 构建目录可能被 IDE 同时改动（qmake 重写 object_script 时会有短暂空档），先做快照；
# qmake 在 Windows 下写 CRLF，必须去掉 \r 否则 -f 判断全部失败
tr -d '\r' < debug/object_script.BlockBoxWindows.Debug > debug/.object_script.snapshot
SNAP_LINES=$(wc -l < debug/.object_script.snapshot)
if [ "$SNAP_LINES" -lt 400 ]; then
  echo "object_script snapshot too short ($SNAP_LINES lines), build dir is being rewritten" >&2
  exit 2
fi
# 排除 main.o（探针自带 main），并跳过尚未编译出来的目标文件
: > "$OBJECT_SCRIPT"
while IFS= read -r obj; do
  [ "$obj" = "debug/main.o" ] && continue
  [ -f "$obj" ] || { echo "skip missing: $obj"; continue; }
  echo "$obj" >> "$OBJECT_SCRIPT"
done < debug/.object_script.snapshot

if [ "${REGEN_QRC}" = "1" ]; then
  echo "=== regenerating qrc resources (style.qss changed) ==="
  "$QTBIN/rcc.exe" -name resources --no-zstd ../../resources.qrc -o debug/qrc_resources_gen.cpp
  g++ -c -fno-keep-inline-dllexport -g -O0 -Wall -Wextra -fexceptions -mthreads \
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
      -o debug/qrc_resources_gen.o debug/qrc_resources_gen.cpp
  sed 's|^debug/qrc_resources\.o$|debug/qrc_resources_gen.o|' "$OBJECT_SCRIPT" > "${OBJECT_SCRIPT}.gen"
  OBJECT_SCRIPT="${OBJECT_SCRIPT}.gen"
fi

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
    -o debug/ai_classic_bg_probe.o ../../tests/ai_classic_bg_probe.cpp

g++ -Wl,-subsystem,console -mthreads -o probe_out/AiClassicBgProbe.exe \
    @"$OBJECT_SCRIPT" debug/ai_classic_bg_probe.o \
    $LIBS_LINE

echo "=== probe built, running ==="
cd probe_out
PROBE_TAG="${PROBE_TAG:-classic_before}" ./AiClassicBgProbe.exe 2>&1 | grep -v "libpng warning\|propagateSizeHints"
exit ${PIPESTATUS[0]}
