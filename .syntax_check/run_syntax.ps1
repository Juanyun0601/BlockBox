param([string[]]$Files)
$CXX = "D:/APP/Qt/Tools/mingw1310_64/bin/g++.exe"
$QT = "D:/APP/Qt/6.11.1/mingw_64"
$DEF = @(
  "-DUNICODE","-D_UNICODE","-DWIN32","-DMINGW_HAS_SECURE_API=1","-DQT_DEPRECATED_WARNINGS",
  "-D_WIN32_WINNT=0x0601","-DWINVER=0x0601","-DWINDOWS_OS","-DQT_NO_DEBUG",
  "-DQT_SVG_LIB","-DQT_OPENGLWIDGETS_LIB","-DQT_OPENGL_LIB","-DQT_WIDGETS_LIB",
  "-DQT_GUI_LIB","-DQT_NETWORK_LIB","-DQT_CONCURRENT_LIB","-DQT_CORE_LIB","-DQT_NEEDS_QMAIN"
)
$FLAGS = @("-fno-keep-inline-dllexport","-O2","-Wall","-Wextra","-fexceptions","-mthreads")
$INC = @(
  "-I.","-Istyles",
  "-I$QT/include","-I$QT/include/QtSvg","-I$QT/include/QtOpenGLWidgets","-I$QT/include/QtOpenGL",
  "-I$QT/include/QtWidgets","-I$QT/include/QtGui","-I$QT/include/QtNetwork",
  "-I$QT/include/QtConcurrent","-I$QT/include/QtCore","-I$QT/mkspecs/win32-g++"
)
$fail = 0
foreach ($f in $Files) {
  $base = [System.IO.Path]::GetFileNameWithoutExtension($f)
  Write-Output "===== $f ====="
  & $CXX -c $f -o ".syntax_check/$base.o" @FLAGS @DEF @INC 2>&1 | Select-Object -Last 40
  if ($LASTEXITCODE -ne 0) { Write-Output "[FAIL] $f"; $fail = 1 } else { Write-Output "[OK] $f" }
}
exit $fail
