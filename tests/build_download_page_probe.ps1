# 多线程下载探针：复用 build-win-release/release 目标文件，只编 tests/download_page_probe.cpp
# 用法：pwsh -NoProfile -File tests\build_download_page_probe.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$env:PATH = 'D:\APP\Qt\6.11.1\mingw_64\bin;D:\APP\Qt\Tools\mingw1310_64\bin;' + $env:PATH

$objDir = 'build-win-release\release'
$objScript = "$objDir\object_script.BlockBoxWindows.Release"
$outDir = '.tmp_download_probe'
$probeObj = "$outDir\probe.o"
$probeExe = "$outDir\download_page_probe.exe"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$qt = 'D:\APP\Qt\6.11.1\mingw_64'
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

Write-Host '=== probe ==='
& g++.exe -c -pipe -fno-keep-inline-dllexport -O1 -fexceptions -mthreads @def @inc -o $probeObj 'tests\download_page_probe.cpp'
if ($LASTEXITCODE -ne 0) { throw 'probe compile failed' }

Write-Host '=== collecting objects ==='
$objects = Get-Content $objScript | Where-Object { $_ -match '\.o$' } |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -ne 'release/main.o' -and $_ -ne 'main.o' } |
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

$final = @('-Wl,-subsystem,console', '-mthreads', '-o', $probeExe) + $objects + @($probeObj) + $libs
Write-Host "objects=$($objects.Count) libargs=$($libs.Count)"

Write-Host '=== linking ==='
$rsp = "$outDir\link.rsp"
$final | ForEach-Object { ($_ -replace '\\', '/') } | Set-Content -Path $rsp -Encoding ASCII
& g++.exe "@$rsp"
if ($LASTEXITCODE -ne 0) { throw 'link failed' }
Write-Host 'PROBE-BUILD-OK'
