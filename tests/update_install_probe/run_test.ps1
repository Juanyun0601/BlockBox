# 更新安装探针：构建 + 端到端验证
#
# 流程：
#   1. 编 marker.exe（扮演"新版本程序"）
#   2. 编 probe.cpp（把 UpdateChecker.cpp 一起编进来）
#   3. 链接成 BlockBoxWindows.exe（排除 main.o / UpdateChecker.o 等）
#   4. 搭一个"假的程序目录"，放旧版本文件 + 用户数据
#   5. 打一个和官方结构一致的更新包（zip 里套一层 BlockBox-<版本>-win64-portable）
#   6. 运行 probe，等辅助脚本完成，再逐项校验
$ErrorActionPreference = 'Stop'

$CD   = 'D:\BlockBox\C-BlockBox'
$QT   = 'D:\APP\Qt\6.11.1\mingw_64'
$MINGW = 'D:\APP\Qt\Tools\mingw1310_64\bin'
$OBJ  = Join-Path $CD 'build-win-release\release'
$DIR  = Join-Path $CD 'tests\update_install_probe'
$OUT  = Join-Path $DIR 'build'
$WORK = Join-Path $DIR 'work'
$APP  = Join-Path $WORK 'app'

$env:PATH = "$QT\bin;$MINGW;$env:PATH"

if (-not (Test-Path $OBJ)) { throw "缺少对象目录：$OBJ（先做一次全量构建）" }
New-Item -ItemType Directory -Force -Path $OUT | Out-Null

$DEF = @(
    '-DUNICODE','-D_UNICODE','-DWIN32','-DMINGW_HAS_SECURE_API=1','-DQT_DEPRECATED_WARNINGS',
    '-D_WIN32_WINNT=0x0601','-DWINVER=0x0601','-DWINDOWS_OS','-DQT_NO_DEBUG',
    '-DQT_SVG_LIB','-DQT_OPENGLWIDGETS_LIB','-DQT_OPENGL_LIB','-DQT_WIDGETS_LIB',
    '-DQT_GUI_LIB','-DQT_NETWORK_LIB','-DQT_CONCURRENT_LIB','-DQT_CORE_LIB'
)
$INC = @(
    "-I$CD","-I$CD\styles","-I$QT\include","-I$QT\include\QtSvg","-I$QT\include\QtOpenGLWidgets",
    "-I$QT\include\QtOpenGL","-I$QT\include\QtWidgets","-I$QT\include\QtGui","-I$QT\include\QtNetwork",
    "-I$QT\include\QtConcurrent","-I$QT\include\QtCore","-I$QT\mkspecs\win32-g++"
)

Write-Host '==== 1. 编译 marker.exe ===='
& g++.exe (Join-Path $DIR 'marker.cpp') -o (Join-Path $OUT 'marker.exe') -O1 -mthreads
if ($LASTEXITCODE -ne 0) { throw 'marker.exe 编译失败' }

Write-Host '==== 2. 编译 probe.o（含 UpdateChecker.cpp）===='
& g++.exe -c (Join-Path $DIR 'probe.cpp') -o (Join-Path $OUT 'probe.o') -O1 -fexceptions -mthreads @DEF @INC
if ($LASTEXITCODE -ne 0) { throw 'probe.o 编译失败' }

Write-Host '==== 3. 链接 BlockBoxWindows.exe ===='
$rsp = Join-Path $OUT 'link.rsp'
$skip = @('main.o','main_probe.o','account_pose_probe.o','UpdateChecker.o')
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add(((Join-Path $OUT 'probe.o') -replace '\\', '/'))
foreach ($o in Get-ChildItem $OBJ -Filter '*.o') {
    if ($skip -contains $o.Name) { continue }
    $lines.Add(($o.FullName -replace '\\', '/'))
}
$qtLibDir = ($QT -replace '\\', '/') + '/lib'
$lines.Add("-L$qtLibDir")
$lines.Add('-lQt6Svg -lQt6OpenGLWidgets -lQt6OpenGL -lQt6Widgets -lQt6Gui -lQt6Network -lQt6Concurrent -lQt6Core')
$lines.Add('-lz -lpdh -liphlpapi -ldwmapi -lgdi32 -lole32 -lshell32 -lshlwapi -luuid -lcomdlg32 -luser32 -ladvapi32 -lws2_32 -loleaut32 -lmingw32')
Set-Content -Path $rsp -Value $lines -Encoding ASCII
& g++.exe "@$rsp" -o (Join-Path $OUT 'BlockBoxWindows.exe') -mthreads '-Wl,--subsystem,console'
if ($LASTEXITCODE -ne 0) { throw '链接失败' }

Write-Host '==== 4. 搭假程序目录 + 更新包 ===='
Remove-Item -Recurse -Force $WORK -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $APP,
    (Join-Path $APP 'BlockBox'),
    (Join-Path $APP '.minecraft'),
    (Join-Path $WORK 'zip\BlockBox-test-win64-portable'),
    (Join-Path $WORK 'zip\BlockBox-test-win64-portable\BlockBox') | Out-Null

Copy-Item (Join-Path $OUT 'BlockBoxWindows.exe') (Join-Path $APP 'BlockBoxWindows.exe')
Set-Content -Path (Join-Path $APP 'DataLib.dll') -Value 'OLD-DLL' -Encoding ASCII
Set-Content -Path (Join-Path $APP 'BlockBox\config.ini') -Value 'USER-CONFIG' -Encoding ASCII
Set-Content -Path (Join-Path $APP 'BlockBox\accounts.json') -Value 'USER-ACCOUNTS' -Encoding ASCII
Set-Content -Path (Join-Path $APP '.minecraft\keep.txt') -Value 'USER-MINECRAFT' -Encoding ASCII

$payload = Join-Path $WORK 'zip\BlockBox-test-win64-portable'
Copy-Item (Join-Path $OUT 'marker.exe') (Join-Path $payload 'BlockBoxWindows.exe')
Set-Content -Path (Join-Path $payload 'DataLib.dll') -Value 'NEW-DLL' -Encoding ASCII
Set-Content -Path (Join-Path $payload 'NewFeature.dll') -Value 'NEW-FILE' -Encoding ASCII
Set-Content -Path (Join-Path $payload 'BlockBox\config.ini') -Value 'FACTORY-CONFIG' -Encoding ASCII
Set-Content -Path (Join-Path $payload 'BlockBox\favorites.json') -Value 'FACTORY-FAVORITES' -Encoding ASCII

$zip = Join-Path $WORK 'BlockBox-test-win64-portable.zip'
Compress-Archive -Path $payload -DestinationPath $zip -Force

Write-Host '==== 5. 运行探针（模拟拖入更新包）===='
Push-Location $APP
& (Join-Path $APP 'BlockBoxWindows.exe') $zip
$probeExit = $LASTEXITCODE
Pop-Location
Write-Host "探针退出码：$probeExit"

Write-Host '==== 6. 等待辅助脚本完成 ===='
$deadline = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $deadline) {
    if (Test-Path (Join-Path $APP 'relaunched.txt')) { break }
    Start-Sleep -Milliseconds 300
}

Write-Host '==== 7. 校验 ===='
$failures = New-Object System.Collections.Generic.List[string]
function Check($name, $expected, $actual) {
    if ($expected -eq $actual) {
        Write-Host ("  [通过] {0} = {1}" -f $name, $actual)
    } else {
        Write-Host ("  [失败] {0}：期望 {1}，实际 {2}" -f $name, $expected, $actual)
        $script:failures.Add($name)
    }
}

$exeReplaced = (Get-FileHash (Join-Path $APP 'BlockBoxWindows.exe')).Hash -eq
               (Get-FileHash (Join-Path $OUT 'marker.exe')).Hash
Check '主程序已替换' $true $exeReplaced
Check '依赖 DLL 已更新' 'NEW-DLL' (Get-Content (Join-Path $APP 'DataLib.dll') -Raw).Trim()
Check '新增文件已落地' 'NEW-FILE' (Get-Content (Join-Path $APP 'NewFeature.dll') -Raw).Trim()
Check '用户配置未被覆盖' 'USER-CONFIG' (Get-Content (Join-Path $APP 'BlockBox\config.ini') -Raw).Trim()
Check '用户账户未被覆盖' 'USER-ACCOUNTS' (Get-Content (Join-Path $APP 'BlockBox\accounts.json') -Raw).Trim()
Check '出厂收藏未塞进用户目录' $false (Test-Path (Join-Path $APP 'BlockBox\favorites.json'))
Check '游戏数据目录未被破坏' 'USER-MINECRAFT' (Get-Content (Join-Path $APP '.minecraft\keep.txt') -Raw).Trim()
Check '更新后已重新启动新版本' 'NEW-VERSION-STARTED' (Get-Content (Join-Path $APP 'relaunched.txt') -Raw -ErrorAction SilentlyContinue).Trim()

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host ("结果：失败 {0} 项 -> {1}" -f $failures.Count, ($failures -join ', '))
    Write-Host '辅助脚本日志：'
    Get-ChildItem $env:TEMP -Filter 'BlockBox-update-*' -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem $_.FullName -Filter '*.log' -Recurse -ErrorAction SilentlyContinue }
    exit 1
}
Write-Host '结果：全部通过'
exit 0
