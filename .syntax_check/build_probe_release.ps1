# 用成熟的 release/ 构建树链接视觉探针（比 build-win-release 更新）。
# 用法：pwsh -NoProfile -File .syntax_check/build_probe_release.ps1 forum_account_probe
param([Parameter(Mandatory = $true)][string]$Name)

$ErrorActionPreference = 'Stop'
$root = 'D:\BlockBox\C-BlockBox'
Set-Location $root

$qt = 'D:/APP/Qt/6.11.1/mingw_64'
$cmakeDir = 'D:/APP/Qt/Tools/mingw1310_64/bin'
$objDir = 'build-win-release/probe_out'
$compileLines = @()
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$defs = '-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS ' +
        '-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG ' +
        '-DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB ' +
        '-DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN'
$incs = "-I. -Istyles -Ithird_party/cubiomes -I$qt/include -I$qt/include/QtSvg " +
        "-I$qt/include/QtOpenGLWidgets -I$qt/include/QtOpenGL -I$qt/include/QtWidgets " +
        "-I$qt/include/QtGui -I$qt/include/QtNetwork -I$qt/include/QtConcurrent " +
        "-I$qt/include/QtCore -I$qt/mkspecs/win32-g++"

# 优先用 Qt Creator 那套新构建树（完整且最新），其次退回 release/
$altRoot = 'build/Desktop_Qt_6_11_1_MinGW_64_bit-Release'
$objRoot = 'release'
$mkFile = 'Makefile.Release'
if (Test-Path "$altRoot/release/object_script.BlockBoxWindows.Release")
{
    $objRoot = "$altRoot/release"
    $mkFile = "$altRoot/Makefile.Release"
    Write-Host "PROBE-OBJECT-TREE: $objRoot"
}
$libs = (Select-String -Path $mkFile -Pattern '^LIBS' | Select-Object -First 1).Line
$libs = $libs.Substring($libs.IndexOf('=') + 1).Trim()

# 对象清单：直接取该构建树里的全部 .o，只剔除 main.o（探针自带 qMain 入口）
$script = New-Object System.Collections.Generic.List[string]
$seen = @{}
foreach ($f in Get-ChildItem $objRoot -Filter '*.o')
{
    if ($f.Name -eq 'main.o' -or $f.Name -eq 'moc_main.o') { continue }
    if ($seen.ContainsKey($f.Name)) { continue }
    $seen[$f.Name] = $true
    $script.Add("$objRoot/$($f.Name)")
}
Write-Host "PROBE-OBJECTS: $($script.Count)"
# 覆盖编译：探针自带的测试源 + 正在改动的页面，编进 probe_out，替换构建树里的旧对象
$overrides = @('pages/ForumAccountPage.cpp')
$extraObjs = New-Object System.Collections.Generic.List[string]
foreach ($src in $overrides)
{
    $base = [IO.Path]::GetFileNameWithoutExtension($src)
    $script = [System.Collections.Generic.List[string]]@($script | Where-Object { $_ -notmatch "/$base\.o$" })
    $extraObjs.Add("$objDir/$base.o")
    $compileLines += "g++ -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads $defs $incs -o $objDir/$base.o $src"
    $compileLines[-1] += "`r`nif errorlevel 1 exit /b 1"
}
foreach ($o in $extraObjs) { $script.Add($o) }
Set-Content -Path "$objDir\object_script.Probe.Release" -Value $script -Encoding ASCII

$batch = @"
set PATH=$cmakeDir;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
set TEMP=D:\BlockBox\C-BlockBox\.tmp_cc
set TMP=D:\BlockBox\C-BlockBox\.tmp_cc
cd /d $root
g++ -c -pipe -fno-keep-inline-dllexport -O2 -Wall -fexceptions -mthreads $defs $incs -o $objDir\$Name.o tests\$Name.cpp
if errorlevel 1 exit /b 1
$($compileLines -join "`r`n")
g++ -Wl,-subsystem,console -mthreads -o $objDir\$Name.exe @$objDir\object_script.Probe.Release $objDir\$Name.o $libs
if errorlevel 1 exit /b 1
echo PROBE-BUILD-OK
"@
$tmp = 'D:\BlockBox\C-BlockBox\.syntax_check\_build_probe.bat'
Set-Content -Path $tmp -Value $batch -Encoding ASCII
cmd /c "$tmp 2>&1" | Select-Object -Last 30
if ($LASTEXITCODE -ne 0) { exit 1 }
