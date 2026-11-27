# 插件管理挪进「设置」导航探针：复用 build-win-release/release 目标文件，只重编被改动的翻译单元
# 用法：pwsh -NoProfile -File tests\build_settings_plugin_nav_probe.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$env:PATH = 'D:\APP\Qt\6.11.1\mingw_64\bin;D:\APP\Qt\Tools\mingw1310_64\bin;' + $env:PATH

$objDir = 'build-win-release\release'
$objScript = "$objDir\object_script.BlockBoxWindows.Release"
$outDir = '.tmp_settings_plugin_nav_probe'
$probeObj = "$outDir\probe.o"
$probeExe = "$outDir\settings_plugin_nav_probe.exe"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$qt = 'D:\APP\Qt\6.11.1\mingw_64'
$moc = "$qt\bin\moc.exe"
$def = @(
    '-DUNICODE','-D_UNICODE','-DWIN32','-DMINGW_HAS_SECURE_API=1','-DQT_DEPRECATED_WARNINGS',
    '-D_WIN32_WINNT=0x0601','-DWINVER=0x0601','-DWINDOWS_OS','-DQT_NO_DEBUG','-DQT_SVG_LIB',
    '-DQT_OPENGLWIDGETS_LIB','-DQT_OPENGL_LIB','-DQT_WIDGETS_LIB','-DQT_GUI_LIB','-DQT_NETWORK_LIB',
    '-DQT_CONCURRENT_LIB','-DQT_CORE_LIB','-DQT_NEEDS_QMAIN'
)
$inc = @(
    '-I.','-Istyles','-Ithird_party/cubiomes',
    "-I$qt\include","-I$qt\include\QtSvg","-I$qt\include\QtOpenGLWidgets","-I$qt\include\QtOpenGL",
    "-I$qt\include\QtWidgets","-I$qt\include\QtGui","-I$qt\include\QtNetwork",
    "-I$qt\include\QtConcurrent","-I$qt\include\QtCore","-I$qt\mkspecs\win32-g++"
)
$mocInc = @(
    "-I$qt\mkspecs\win32-g++", "-I$root", "-I$root\styles", "-I$qt\include",
    "-I$qt\include\QtSvg","-I$qt\include\QtOpenGLWidgets","-I$qt\include\QtOpenGL",
    "-I$qt\include\QtWidgets","-I$qt\include\QtGui","-I$qt\include\QtNetwork",
    "-I$qt\include\QtConcurrent","-I$qt\include\QtCore"
)

function Compile($src, $obj) {
    Write-Host "=== compile $src ==="
    & g++.exe -c -pipe -fno-keep-inline-dllexport -O1 -fexceptions -mthreads @def @inc -o $obj $src
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
}

function RunMoc($header, $cppOut, $objOut) {
    Write-Host "=== moc $header ==="
    & $moc @def --include "$objDir\moc_predefs.h" @mocInc $header -o $cppOut
    if ($LASTEXITCODE -ne 0) { throw "moc failed: $header" }
    Compile $cppOut $objOut
}

Write-Host '=== probe ==='
Compile 'tests\settings_plugin_nav_probe.cpp' $probeObj

$overrides = [ordered]@{
    'mainwindow.o'      = 'mainwindow.cpp'
    'MainWindowPages.o' = 'MainWindowPages.cpp'
    'SideBar.o'         = 'components\SideBar.cpp'
    'SettingsPage.o'    = 'pages\SettingsPage.cpp'
    'MorePage.o'        = 'pages\MorePage.cpp'
    'PluginPage.o'      = 'pages\PluginPage.cpp'
}
foreach ($name in $overrides.Keys) {
    Compile $overrides[$name] "$outDir\$name"
}

RunMoc 'mainwindow.h' "$outDir\moc_mainwindow.cpp" "$outDir\moc_mainwindow.o"
RunMoc 'pages\SettingsPage.h' "$outDir\moc_SettingsPage.cpp" "$outDir\moc_SettingsPage.o"
RunMoc 'pages\MorePage.h' "$outDir\moc_MorePage.cpp" "$outDir\moc_MorePage.o"

Write-Host '=== building link args ==='
$objects = Get-Content $objScript | Where-Object { $_ -match '\.o$' } |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -ne 'release/main.o' -and $_ -ne 'main.o' } |
    Where-Object { -not $overrides.Contains((Split-Path -Leaf ($_ -replace '\\', '/'))) } |
    Where-Object { (Split-Path -Leaf ($_ -replace '\\', '/')) -notmatch '^moc_(mainwindow|SettingsPage|MorePage)\.o$' } |
    ForEach-Object { "$objDir\" + ($_ -replace '^release[\\/]', '') }

$linkLine = (Get-Content 'build-win-release\link_fix.bat' | Where-Object { $_ -match '^g\+\+' } | Select-Object -First 1)
$libs = @()
$started = $false
foreach ($a in ($linkLine -split '\s+')) {
    if ($a -eq '') { continue }
    if (-not $started) {
        if ($a -match 'libQt6' -or $a -match '\.a$' -or $a -match '^-l') { $started = $true }
        else { continue }
    }
    if ($a -match '^echo$' -or $a -match 'ERRORLEVEL') { break }
    $libs += $a
}

$final = @('-Wl,-subsystem,console', '-mthreads', '-o', $probeExe) + $objects + @($probeObj) +
    ($overrides.Keys | ForEach-Object { "$outDir\$_" }) +
    @("$outDir\moc_mainwindow.o", "$outDir\moc_SettingsPage.o", "$outDir\moc_MorePage.o") + $libs
Write-Host "objects=$($objects.Count) libargs=$($libs.Count)"

Write-Host '=== linking ==='
$rsp = "$outDir\link.rsp"
$final | ForEach-Object { ($_ -replace '\\', '/') } | Set-Content -Path $rsp -Encoding ASCII
& g++.exe "@$rsp"
if ($LASTEXITCODE -ne 0) { throw 'link failed' }
Write-Host 'PROBE-BUILD-OK'
