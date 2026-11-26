# AI 页对话侧栏探针：复用 build-win-release/release 目标文件链接探针可执行文件
# 对象清单取 object_script（qmake 生成的权威列表），链接库沿用 link_fix.bat
param([string]$ObjDir = 'build-win-release\release')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$env:PATH = 'D:\APP\Qt\6.11.1\mingw_64\bin;D:\APP\Qt\Tools\mingw1310_64\bin;' + $env:PATH

$objDir = $ObjDir
$objScript = "$objDir\object_script.BlockBoxWindows.Release"
$outDir = '.tmp_ai_sidebar_probe'
$probeObj = "$outDir\probe.o"
$probeExe = "$outDir\probe.exe"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$def = @(
    '-DUNICODE','-D_UNICODE','-DWIN32','-DMINGW_HAS_SECURE_API=1','-DQT_DEPRECATED_WARNINGS',
    '-D_WIN32_WINNT=0x0601','-DWINVER=0x0601','-DWINDOWS_OS','-DQT_NO_DEBUG','-DQT_SVG_LIB',
    '-DQT_OPENGLWIDGETS_LIB','-DQT_OPENGL_LIB','-DQT_WIDGETS_LIB','-DQT_GUI_LIB','-DQT_NETWORK_LIB',
    '-DQT_CONCURRENT_LIB','-DQT_CORE_LIB','-DQT_NEEDS_QMAIN'
)
$qt = 'D:\APP\Qt\6.11.1\mingw_64'
$inc = @(
    '-I.','-Istyles','-Ithird_party/cubiomes',
    "-I$qt\include","-I$qt\include\QtSvg","-I$qt\include\QtOpenGLWidgets","-I$qt\include\QtOpenGL",
    "-I$qt\include\QtWidgets","-I$qt\include\QtGui","-I$qt\include\QtNetwork",
    "-I$qt\include\QtConcurrent","-I$qt\include\QtCore","-I$qt\mkspecs\win32-g++"
)

Write-Host '=== compiling probe ==='
& g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads @def @inc `
    -o $probeObj tests\assistant_ai_sidebar_probe.cpp
if ($LASTEXITCODE -ne 0) { throw 'compile failed' }

# 改了源码的翻译单元（别去动 build-win-release 里共享的 .o，那边可能正被别的构建占用）
$overrides = [ordered]@{
    'AiChatPage.o' = 'pages\AiChatPage.cpp'
}
foreach ($name in $overrides.Keys) {
    Write-Host "=== compiling override $name ==="
    & g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads @def @inc `
        -o "$outDir\$name" $overrides[$name]
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $name" }
}

Write-Host '=== building link args ==='
# 1) 对象：object_script 里 release/xxx.o，去掉 main.o（由探针提供 qMain）和被覆盖的那些
$objects = Get-Content $objScript | Where-Object { $_ -match '\.o$' } |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -ne 'release/main.o' -and $_ -ne 'main.o' } |
    Where-Object { -not $overrides.Contains((Split-Path -Leaf ($_ -replace '\\', '/'))) } |
    ForEach-Object { "$objDir\" + ($_ -replace '^release[\\/]', '') }

# 2) 库：link_fix.bat 里抽出 Qt/系统库部分（丢掉对象文件和输出参数）
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

# -o 目标放在最前，其余沿用 link_fix.bat 的顺序（对象 → Qt 库 → 系统库）
$final = @('-Wl,-subsystem,console', '-mthreads', '-o', $probeExe) + $objects + @($probeObj) +
    ($overrides.Keys | ForEach-Object { "$outDir\$_" }) + $libs
Write-Host "objects=$($objects.Count) libargs=$($libs.Count)"
$final | Set-Content -Path "$outDir\args.txt" -Encoding ASCII

Write-Host '=== linking ==='
$rsp = "$outDir\link.rsp"
# binutils 的响应文件里反斜杠是转义符，路径统一换成 /
$final | ForEach-Object { ($_ -replace '\\', '/') } | Set-Content -Path $rsp -Encoding ASCII
& g++.exe "@$rsp"
if ($LASTEXITCODE -ne 0) { throw 'link failed' }
Write-Host 'PROBE-BUILD-OK'


