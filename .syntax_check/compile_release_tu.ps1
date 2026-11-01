# 用 build-win-release 的 Release 参数把单个 .cpp 编译进 build-win-release/release/。
# 注意：必须把 mingw 的 bin 放进 PATH 再调用 `g++`（直接用绝对路径调用会静默失败）。
# 用法：pwsh -NoProfile -File .syntax_check/compile_release_tu.ps1 pages/ForumAccountPage.cpp
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$OutputDir = 'build-win-release/release'
)

$ErrorActionPreference = 'Stop'
$root = 'D:\BlockBox\C-BlockBox'
Set-Location $root

$name = [System.IO.Path]::GetFileNameWithoutExtension($Source)
$obj = "$OutputDir/$name.o"

$commands = @(
    'set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;%PATH%',
    "cd /d $root",
    ("g++ -c $Source -o $obj -fno-keep-inline-dllexport -O2 -Wall -Wextra -fexceptions -mthreads " +
     "-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS " +
     "-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG " +
     "-DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB " +
     "-DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN " +
     "-I. -Istyles -Ithird_party/cubiomes " +
     "-ID:/APP/Qt/6.11.1/mingw_64/include -ID:/APP/Qt/6.11.1/mingw_64/include/QtSvg " +
     "-ID:/APP/Qt/6.11.1/mingw_64/include/QtOpenGLWidgets -ID:/APP/Qt/6.11.1/mingw_64/include/QtOpenGL " +
     "-ID:/APP/Qt/6.11.1/mingw_64/include/QtWidgets -ID:/APP/Qt/6.11.1/mingw_64/include/QtGui " +
     "-ID:/APP/Qt/6.11.1/mingw_64/include/QtNetwork -ID:/APP/Qt/6.11.1/mingw_64/include/QtConcurrent " +
     "-ID:/APP/Qt/6.11.1/mingw_64/include/QtCore -ID:/APP/Qt/6.11.1/mingw_64/mkspecs/win32-g++")
)

$batch = ($commands -join "`r`n")
$tmp = Join-Path $env:TEMP 'bb_compile_tu.bat'
Set-Content -Path $tmp -Value $batch -Encoding ASCII
cmd /c "$tmp 2>&1" | ForEach-Object { $_ }
$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Output "COMPILE-FAILED ($Source)"
    exit $code
}
Write-Output "COMPILE-OK -> $obj"
