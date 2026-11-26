# 编译并运行「DSH 插件一键安装」文件逻辑探针（不需要启动界面）
$ErrorActionPreference = 'Stop'

$qtBin = 'D:\APP\Qt\6.11.1\mingw_64\bin'
$qtInclude = 'D:\APP\Qt\6.11.1\mingw_64\include'
$qtLib = 'D:\APP\Qt\6.11.1\mingw_64\lib'
$mingw = 'D:\APP\Qt\Tools\mingw1310_64\bin'
$gxx = Join-Path $mingw 'g++.exe'

$root = 'D:\BlockBox\C-BlockBox'
$payload = 'D:\BlockBox\dsh-blockbox-bridge'
$outDir = Join-Path $root '.tmp_dsh_probe'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$exe = Join-Path $outDir 'dsh_plugin_install_probe.exe'

# 编译器要在 PATH 上才找得到 cc1plus / as / ld
$env:PATH = "$mingw;$qtBin;$env:PATH"

$common = @(
    '-std=c++17', '-O1', '-Wall', '-Wextra',
    '-DUNICODE', '-D_UNICODE', '-DWIN32', '-DQT_CORE_LIB', '-DQT_NO_DEBUG', '-DMINGW_HAS_SECURE_API=1',
    "-I$root",
    "-I$qtInclude", "-I$qtInclude/QtCore",
    "-I$qtInclude/mkspecs/win32-g++"
)

& $gxx @common `
    (Join-Path $root 'tests/dsh_plugin_install_probe.cpp') `
    (Join-Path $root 'utils/dsh/DshPluginInstaller.cpp') `
    "-L$qtLib" -lQt6Core `
    '-o' $exe
if ($LASTEXITCODE -ne 0) { throw "编译失败 exit=$LASTEXITCODE" }

$env:PATH = "$qtBin;$mingw;$env:PATH"
& $exe $payload
$code = $LASTEXITCODE
Write-Output "probe exit=$code"
exit $code
