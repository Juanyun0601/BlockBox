#!/bin/bash
# 构建并运行 AI 页底部输入区端到端探针（链接主程序全部目标文件，仅替换 main.cpp）
# 用法:
#   ./run_ai_bottom_probe.sh              # 用现有资源（改动前）
#   REGEN_QRC=1 ./run_ai_bottom_probe.sh  # 先用 rcc 重新生成 style.qss 资源再链接（改动后）
set -e
BUILDDIR="D:/BlockBox/C-BlockBox/build/Desktop_Qt_6_11_1_MinGW_64_bit-Debug"
QTBIN="/d/APP/Qt/6.11.1/mingw_64/bin"
MINGWBIN="/d/APP/Qt/Tools/mingw1310_64/bin"
export PATH="$MINGWBIN:$QTBIN:$PATH"
export TMP="/d/Temp/blockbox_build" TEMP="/d/Temp/blockbox_build" TMPDIR="/d/Temp/blockbox_build"

cd "$BUILDDIR"

QRC_OBJ="debug/qrc_resources.o"
OBJECT_SCRIPT="debug/object_script.AiBottomProbe.Debug"
grep -v "^debug/main\.o$" debug/object_script.BlockBoxWindows.Debug > "$OBJECT_SCRIPT"

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
  # 链接脚本中用新生成的资源对象替换旧的
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
    -o debug/ai_chat_bottom_probe.o ../../tests/ai_chat_bottom_probe.cpp

g++ -Wl,-subsystem,console -mthreads -o probe_out/AiChatBottomProbe.exe \
    @"$OBJECT_SCRIPT" debug/ai_chat_bottom_probe.o \
    $LIBS_LINE

echo "=== probe built, running ==="
cd probe_out
PROBE_TAG="${PROBE_TAG:-before}" ./AiChatBottomProbe.exe 2>&1 | grep -v "libpng warning\|propagateSizeHints"
exit ${PIPESTATUS[0]}
