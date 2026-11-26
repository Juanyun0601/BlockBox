#!/bin/bash
# 临时自测：用本机 http 服务验证「首页预设封面图下载」流程
# 不联网、不改产品代码：运行期用 BLOCKBOX_HOME_COVER_BASE_URL 把取图来源指到本机静态服务。
set -e
export PATH="/d/APP/Qt/Tools/mingw1310_64/bin:/d/APP/Qt/6.11.1/mingw_64/bin:$PATH"
QT=/d/APP/Qt/6.11.1/mingw_64
ROOT=/d/BlockBox/C-BlockBox
WORK=/tmp/home_covers_dl
PORT=8765
SRC=/d/BlockBox/方块盒子官网/assets/home-covers

rm -rf "$WORK"; mkdir -p "$WORK"

# 1) 起本机静态服务，充当取图来源
cd "$SRC"
python -m http.server $PORT > "$WORK/http.log" 2>&1 &
HTTP_PID=$!
trap 'kill $HTTP_PID 2>/dev/null || true' EXIT
sleep 2
echo "[http] serving $SRC on $PORT"

# 2) moc
cd "$WORK"
moc "$ROOT/utils/HomePresetCovers.h" -o moc_HomePresetCovers.cpp

# 3) 编译（HomePresetCovers 只依赖 Qt Core/Gui/Network）
g++ -fexceptions -mthreads -DUNICODE -D_UNICODE -DWIN32 -DQT_NO_DEBUG \
    -DQT_NETWORK_LIB -DQT_GUI_LIB -DQT_CORE_LIB \
    -I"$ROOT" -I"$QT/include" -I"$QT/include/QtNetwork" -I"$QT/include/QtGui" -I"$QT/include/QtCore" \
    -I"$QT/mkspecs/win32-g++" -I. \
    -o hpc_test.exe "$ROOT/utils/HomePresetCovers.cpp" moc_HomePresetCovers.cpp \
    "$ROOT/tests/home_preset_covers_download_test.cpp" \
    -L"$QT/lib" -lQt6Network -lQt6Gui -lQt6Core -lmingw32

echo "[build] ok"
# 单次运行即可；输出直接落到终端，别再用管道包一层（后台 http 服务会让管道不结束）
set +e
BLOCKBOX_HOME_COVER_BASE_URL="http://127.0.0.1:$PORT/" ./hpc_test.exe
code=$?
set -e
echo "[exit] $code"
exit $code
