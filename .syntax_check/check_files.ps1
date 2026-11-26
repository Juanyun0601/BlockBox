param([string[]]$Files)

# 语法体检：用 Qt 的 mingw g++ 逐个 -c 编译（不链接），只验证改动过的文件能不能过编译
# 注意：mingw 的 bin 目录必须进 PATH，否则 g++ 拉起的 cc1plus 找不到运行库、静默失败
$MINGW = "D:\APP\Qt\Tools\mingw1310_64\bin"
$env:PATH = "$MINGW;" + $env:PATH

$QT  = "D:/APP/Qt/6.11.1/mingw_64"
$DEF = @(
  "-DUNICODE","-D_UNICODE","-DWIN32","-DMINGW_HAS_SECURE_API=1","-DQT_DEPRECATED_WARNINGS",
  "-D_WIN32_WINNT=0x0601","-DWINVER=0x0601","-DWINDOWS_OS","-DQT_NO_DEBUG",
  "-DQT_SVG_LIB","-DQT_OPENGLWIDGETS_LIB","-DQT_OPENGL_LIB","-DQT_WIDGETS_LIB","-DQT_GUI_LIB",
  "-DQT_NETWORK_LIB","-DQT_CONCURRENT_LIB","-DQT_CORE_LIB","-DQT_NEEDS_QMAIN"
)
$INC = @(
  "-I.","-Istyles",
  "-I$QT/include","-I$QT/include/QtSvg","-I$QT/include/QtOpenGLWidgets","-I$QT/include/QtOpenGL",
  "-I$QT/include/QtWidgets","-I$QT/include/QtGui","-I$QT/include/QtNetwork",
  "-I$QT/include/QtConcurrent","-I$QT/include/QtCore","-I$QT/mkspecs/win32-g++"
)
$FLAGS = @("-c","-fno-keep-inline-dllexport","-O0","-fexceptions","-mthreads","-w")

$fail = 0
foreach ($f in $Files) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($f)
    "===== $f"
    $out = & g++.exe -c $f -o ".syntax_check/$base.chk.o" @FLAGS @DEF @INC 2>&1
    $code = $LASTEXITCODE
    if ($code -eq 0) {
        "  [OK] $f"
    } else {
        "  [FAIL] $f (exit $code)"
        $out | Set-Content ".syntax_check/$base.err.txt"
        $out | Select-Object -First 25
        $fail = 1
    }
}
if ($fail -eq 0) { "ALL OK" } else { "SOME FAILED" }
exit $fail
