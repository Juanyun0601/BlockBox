# 迭代式最小链接：先给一小撮目标文件，按 undefined reference 逐步补 .o / -l
param(
    [string[]]$Objects,
    [switch]$ShowUndefined
)
$ErrorActionPreference = 'Continue'
$bd = 'D:\BlockBox\C-BlockBox\build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug'
$gpp = 'D:\APP\Qt\Tools\mingw1310_64\bin\g++.exe'
$env:PATH = "D:\APP\Qt\Tools\mingw1310_64\bin;$env:PATH"
Set-Location $bd

$libs = @(
    'D:\APP\Qt\6.11.1\mingw_64\lib\libQt6OpenGLWidgets.a',
    'D:\APP\Qt\6.11.1\mingw_64\lib\libQt6OpenGL.a',
    'D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Widgets.a',
    'D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Gui.a',
    'D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Network.a',
    'D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Concurrent.a',
    'D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Core.a',
    'D:\APP\Qt\6.11.1\mingw_64\lib\libQt6EntryPoint.a',
    '-lz', '-lpdh', '-liphlpapi', '-ldwmapi', '-lgdi32', '-lole32', '-lshell32',
    '-lshlwapi', '-luuid', '-lcomdlg32', '-luser32', '-ladvapi32', '-lws2_32', '-loleaut32',
    '-lmingw32'
)

$out = & $gpp '-Wl,-subsystem,windows' '-mthreads' '-o' 'probe_pbed_ui.exe' $Objects $libs 2>&1
$code = $LASTEXITCODE
$undef = ($out | Select-String -Pattern "undefined reference to ``(.+)'" -AllMatches |
    ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value }) | Sort-Object -Unique
if ($code -eq 0) { Write-Host "LINK OK" } else {
    Write-Host "LINK FAIL exit=$code"
    if ($ShowUndefined) { $undef | ForEach-Object { Write-Host "UNDEF $_" } }
    else { Write-Host ("undefined count=" + ($undef | Measure-Object).Count); $undef | Select-Object -First 40 | ForEach-Object { Write-Host "UNDEF $_" } }
    $out | Where-Object { $_ -notmatch 'undefined reference' } | Select-Object -First 10 | ForEach-Object { Write-Host "MSG $_" }
}
