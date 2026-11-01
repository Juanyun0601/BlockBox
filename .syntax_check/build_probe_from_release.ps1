# 用 release/ 构建树链接任意探针（这里用于论坛账户页接线探针）。
# 用法：pwsh -NoProfile -File .syntax_check/build_probe_from_release.ps1 -Name forum_account_page_probe
param(
    [string]$Name = 'forum_account_page_probe',
    [string[]]$Overrides = @()
)

$ErrorActionPreference = 'Stop'
$root = 'D:\BlockBox\C-BlockBox'
Set-Location $root

$qt = 'D:/APP/Qt/6.11.1/mingw_64'
$mingw = 'D:/APP/Qt/Tools/mingw1310_64/bin'
$objDir = 'build-win-release/probe_out'
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$defs = '-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS ' +
        '-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG ' +
        '-DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB ' +
        '-DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN'
$incs = "-I. -Istyles -Ithird_party/cubiomes -I$qt/include -I$qt/include/QtSvg " +
        "-I$qt/include/QtOpenGLWidgets -I$qt/include/QtOpenGL -I$qt/include/QtWidgets " +
        "-I$qt/include/QtGui -I$qt/include/QtNetwork -I$qt/include/QtConcurrent " +
        "-I$qt/include/QtCore -I$qt/mkspecs/win32-g++"

$objRoot = 'release'
$mkFile = 'Makefile.Release'
$libs = (Select-String -Path $mkFile -Pattern '^LIBS' | Select-Object -First 1).Line
$libs = $libs.Substring($libs.IndexOf('=') + 1).Trim()

$script = New-Object System.Collections.Generic.List[string]
$seen = @{}
# 只信任 qmake 生成的 object_script（release/ 里还躺着旧的 *.cpp.o，重复链接会撞符号）
$objScript = "$objRoot/object_script.BlockBoxWindows.Release"
foreach ($line in Get-Content $objScript)
{
    $obj = $line.Trim()
    if (-not $obj) { continue }
    $objName = [IO.Path]::GetFileName($obj)
    if ($objName -eq 'main.o' -or $objName -eq 'moc_main.o') { continue }
    if ($objName -eq 'BlockBoxWindows_resource_res.o') { continue }
    if ($seen.ContainsKey($objName)) { continue }
    $seen[$objName] = $true
    $script.Add($obj)
}
Write-Host "PROBE-OBJECTS: $($script.Count)"

$extraObjs = New-Object System.Collections.Generic.List[string]
$compileLines = @()
foreach ($src in $Overrides)
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
set PATH=$mingw;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
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
$tmp = "$root\.syntax_check\_build_probe_from_release.bat"
Set-Content -Path $tmp -Value $batch -Encoding ASCII
cmd /c "$tmp 2>&1" | Select-Object -Last 60
if ($LASTEXITCODE -ne 0) { exit 1 }
