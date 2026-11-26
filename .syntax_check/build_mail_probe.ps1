# 用 build-win-release/release 里最新的对象链接「邮件反馈」截图探针。
# 与 build_probe_from_release.ps1 同类，只是对象目录换成当前最新的构建树。
param(
    [string]$Name = 'mail_feedback_probe'
)

$ErrorActionPreference = 'Stop'
$root = 'D:\BlockBox\C-BlockBox'
Set-Location $root

$qt = 'D:/APP/Qt/6.11.1/mingw_64'
$mingw = 'D:/APP/Qt/Tools/mingw1310_64/bin'
$objDir = 'build-win-release/probe_out'
$relDir = 'build-win-release/release'
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$defs = '-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS ' +
        '-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG ' +
        '-DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB ' +
        '-DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN'
$incs = "-I. -Istyles -Ithird_party/cubiomes -I$qt/include -I$qt/include/QtSvg " +
        "-I$qt/include/QtOpenGLWidgets -I$qt/include/QtOpenGL -I$qt/include/QtWidgets " +
        "-I$qt/include/QtGui -I$qt/include/QtNetwork -I$qt/include/QtConcurrent " +
        "-I$qt/include/QtCore -I$qt/mkspecs/win32-g++"
$compile = "-c -pipe -fno-keep-inline-dllexport -O2 -Wall -fexceptions -mthreads $defs $incs"

$libs = (Select-String -Path 'build-win-release/Makefile.Release' -Pattern '^LIBS' |
        Select-Object -First 1).Line
$libs = $libs.Substring($libs.IndexOf('=') + 1).Trim()

# 对象清单：排除 main.o / 资源对象，换成新构建树路径并去重
$seen = @{}
$objects = New-Object System.Collections.Generic.List[string]
foreach ($line in Get-Content "$relDir/object_script.BlockBoxWindows.Release")
{
    $obj = $line.Trim()
    if (-not $obj) { continue }
    $objName = [IO.Path]::GetFileName($obj)
    if ($objName -eq 'main.o' -or $objName -eq 'moc_main.o') { continue }
    if ($objName -eq 'BlockBoxWindows_resource_res.o') { continue }
    if ($seen.ContainsKey($objName)) { continue }
    $seen[$objName] = $true
    $objects.Add("build-win-release/$obj")
}
Write-Host "PROBE-OBJECTS: $($objects.Count)"

# 探针自身 + 新对话框（同名对象从旧清单里剔除，用新编译的覆盖）
$extra = @('components/MailFeedbackDialog.cpp')
$compileLines = New-Object System.Collections.Generic.List[string]
foreach ($src in $extra)
{
    $base = [IO.Path]::GetFileNameWithoutExtension($src)
    $objects = [System.Collections.Generic.List[string]]@($objects |
        Where-Object { $_ -notmatch "/(moc_)?$base\.o$" })
    $objects.Add("$objDir/$base.o")
    $compileLines.Add("g++ $compile -o $objDir/$base.o $src")
    $compileLines.Add('if errorlevel 1 exit /b 1')
}

# 对话框带 Q_OBJECT，moc 单独生成
$mocOut = "$objDir/moc_MailFeedbackDialog.cpp"
& "$qt/bin/moc.exe" $defs.Split(' ') -I. -Istyles "-I$qt/include" "-I$qt/include/QtCore" `
    "-I$qt/mkspecs/win32-g++" -o $mocOut components/MailFeedbackDialog.h
if ($LASTEXITCODE -ne 0) { throw 'moc failed' }
$objects.Add("$objDir/moc_MailFeedbackDialog.o")
$compileLines.Add("g++ $compile -o $objDir/moc_MailFeedbackDialog.o $mocOut")
$compileLines.Add('if errorlevel 1 exit /b 1')

Set-Content -Path "$objDir/object_script.MailProbe.Release" -Value $objects -Encoding ASCII

$batch = @"
set PATH=$mingw;$qt/bin;%PATH%
set TEMP=$root\.tmp_cc
set TMP=$root\.tmp_cc
cd /d $root
g++ $compile -o $objDir\$Name.o tests\$Name.cpp
if errorlevel 1 exit /b 1
$($compileLines -join "`r`n")
g++ -Wl,-subsystem,console -mthreads -o $objDir\$Name.exe @$objDir\object_script.MailProbe.Release $objDir\$Name.o $libs
if errorlevel 1 exit /b 1
echo PROBE-BUILD-OK
"@
$tmp = "$root\.syntax_check\_build_mail_probe.bat"
Set-Content -Path $tmp -Value $batch -Encoding ASCII
cmd /c "$tmp 2>&1" | Select-Object -Last 40
if ($LASTEXITCODE -ne 0) { exit 1 }
