# 用成熟的 release/ 构建树链接「聊天设置弹窗」探针。
# 用法：pwsh -NoProfile -File .syntax_check/build_chat_settings_probe.ps1
param([string]$Name = 'chat_settings_probe')

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
$incs = "-I. -Icomponents -Istyles -Ithird_party/cubiomes -I$qt/include -I$qt/include/QtSvg " +
        "-I$qt/include/QtOpenGLWidgets -I$qt/include/QtOpenGL -I$qt/include/QtWidgets " +
        "-I$qt/include/QtGui -I$qt/include/QtNetwork -I$qt/include/QtConcurrent " +
        "-I$qt/include/QtCore -I$qt/mkspecs/win32-g++"

$objRoot = 'release'
$mkFile = 'Makefile.Release'
# 优先用 Qt Creator 那套更新更快的构建树（旧 release/ 里的主窗口对象已经和头文件对不上了）
$altRoot = 'build/Desktop_Qt_6_11_1_MinGW_64_bit-Release'
if (Test-Path "$altRoot/release/object_script.BlockBoxWindows.Release")
{
    $objRoot = "$altRoot/release"
    $mkFile = "$altRoot/Makefile.Release"
}
Write-Host "PROBE-OBJ-TREE: $objRoot"
$libs = (Select-String -Path $mkFile -Pattern '^LIBS' | Select-Object -First 1).Line
$libs = $libs.Substring($libs.IndexOf('=') + 1).Trim()

# 本次新增 / 改动的源文件（这份构建树里的同名对象陈旧或缺失，全部现场重编）
$overrides = @('components/ChatSettingsDialog.cpp', 'utils/ChatPrefs.cpp',
               'utils/ChatUiStore.cpp', 'components/AppDialogBase.cpp',
               'utils/ForumChatService.cpp', 'pages/ForumFriendsPage.cpp',
               'utils/ChatBookmarkStore.cpp', 'components/ForumKit.cpp',
               'components/ChatBookmarkDialog.cpp', 'components/ChatForwardDialog.cpp',
               'components/CreateGroupDialog.cpp')
$mocHeaders = @('components/ChatSettingsDialog.h', 'utils/ChatPrefs.h',
                'utils/ChatUiStore.h', 'components/AppDialogBase.h',
                'utils/ForumChatService.h', 'pages/ForumFriendsPage.h',
                'utils/ChatBookmarkStore.h', 'components/ForumKit.h',
                'components/ChatBookmarkDialog.h', 'components/ChatForwardDialog.h',
                'components/CreateGroupDialog.h')

$dropNames = @{}
foreach ($src in $overrides) { $dropNames[[IO.Path]::GetFileNameWithoutExtension($src)] = $true }
foreach ($hdr in $mocHeaders) { $dropNames['moc_' + [IO.Path]::GetFileNameWithoutExtension($hdr)] = $true }

$script = New-Object System.Collections.Generic.List[string]
$seen = @{}
foreach ($f in Get-ChildItem $objRoot -Filter '*.o')
{
    if ($f.Name -eq 'main.o' -or $f.Name -eq 'moc_main.o') { continue }
    if ($f.Name -eq 'BlockBoxWindows_resource_res.o') { continue }
    $base = [IO.Path]::GetFileNameWithoutExtension($f.Name)
    if ($dropNames.ContainsKey($base)) { continue }
    if ($seen.ContainsKey($f.Name)) { continue }
    $seen[$f.Name] = $true
    $script.Add("$objRoot/$($f.Name)")
}

foreach ($src in $overrides)
{
    $base = [IO.Path]::GetFileNameWithoutExtension($src)
    $script.Add("$objDir/$base.o")
    $compileLines += "g++ -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads $defs $incs -o $objDir/$base.o $src"
    $compileLines[-1] += "`r`nif errorlevel 1 exit /b 1"
}
Write-Host "PROBE-OBJECTS: $($script.Count)"

$mocLines = @()
foreach ($hdr in $mocHeaders)
{
    $base = 'moc_' + [IO.Path]::GetFileNameWithoutExtension($hdr)
    $cpp = "$objDir/$base.cpp"
    $obj = "$objDir/$base.o"
    $script.Add($obj)
    $mocLines += "moc.exe $defs $incs -o $cpp $hdr"
    $mocLines[-1] += "`r`nif errorlevel 1 exit /b 1"
    $mocLines += "g++ -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads $defs $incs -o $obj $cpp"
    $mocLines[-1] += "`r`nif errorlevel 1 exit /b 1"
}
Set-Content -Path "$objDir\object_script.Probe.Release" -Value $script -Encoding ASCII

$batch = @"
set PATH=$cmakeDir;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
set TEMP=D:\BlockBox\C-BlockBox\.tmp_cc
set TMP=D:\BlockBox\C-BlockBox\.tmp_cc
cd /d $root
g++ -c -pipe -fno-keep-inline-dllexport -O2 -Wall -fexceptions -mthreads $defs $incs -o $objDir\$Name.o tests\$Name.cpp
if errorlevel 1 exit /b 1
$($compileLines -join "`r`n")
$($mocLines -join "`r`n")
g++ -Wl,-subsystem,console -mthreads -o $objDir\$Name.exe @$objDir\object_script.Probe.Release $objDir\$Name.o $libs
if errorlevel 1 exit /b 1
echo PROBE-BUILD-OK
"@
$tmp = 'D:\BlockBox\C-BlockBox\.syntax_check\_build_chat_settings_probe.bat'
Set-Content -Path $tmp -Value $batch -Encoding ASCII
cmd /c "$tmp 2>&1" | Select-Object -Last 25
if ($LASTEXITCODE -ne 0) { exit 1 }
