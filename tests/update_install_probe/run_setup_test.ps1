# 更新探针：setup.exe（Inno 安装包）路径的端到端验证
#
# 造一个独立的测试安装包（单独 AppId、不要管理员权限），让真实的
# 「准备 → 等进程退出 → 静默安装 → 还原用户数据 → 重新启动」流程跑一遍。
$ErrorActionPreference = 'Stop'

$DIR  = 'D:\BlockBox\C-BlockBox\tests\update_install_probe'
$OUT  = Join-Path $DIR 'build'
$WORK = Join-Path $DIR 'setupwork'
$APP  = Join-Path $WORK 'app'
$SRC  = Join-Path $WORK 'payload'
$ISCC = 'D:\APP\InnoSetup\ISCC.exe'
$env:PATH = "D:\APP\Qt\6.11.1\mingw_64\bin;D:\APP\Qt\Tools\mingw1310_64\bin;$env:PATH"

if (-not (Test-Path (Join-Path $OUT 'BlockBoxWindows.exe'))) {
    throw '先跑 run_test.ps1 生成探针 exe'
}
if (-not (Test-Path $ISCC)) { throw "找不到 ISCC：$ISCC" }

Remove-Item -Recurse -Force $WORK -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $APP, (Join-Path $APP 'BlockBox'), $SRC,
    (Join-Path $SRC 'BlockBox') | Out-Null

Copy-Item (Join-Path $OUT 'BlockBoxWindows.exe') (Join-Path $APP 'BlockBoxWindows.exe')
Set-Content -Path (Join-Path $APP 'BlockBox\config.ini') -Value 'USER-CONFIG' -Encoding ASCII
Set-Content -Path (Join-Path $APP 'BlockBox\accounts.json') -Value 'USER-ACCOUNTS' -Encoding ASCII

# 安装包内容：新版本替身 + 出厂数据目录（和官方包一样会带一份默认 BlockBox 目录）
Copy-Item (Join-Path $OUT 'marker.exe') (Join-Path $SRC 'BlockBoxWindows.exe')
Set-Content -Path (Join-Path $SRC 'BlockBox\config.ini') -Value 'FACTORY-CONFIG' -Encoding ASCII
Set-Content -Path (Join-Path $SRC 'BlockBox\favorites.json') -Value 'FACTORY-FAVORITES' -Encoding ASCII

Write-Host '==== 1. 编译测试安装包 ===='
& $ISCC /Qp "/DSourceDir=$SRC" "/DOutputDir=$WORK" (Join-Path $DIR 'probe_setup.iss') | Out-Null
if ($LASTEXITCODE -ne 0) { throw '安装包编译失败' }
$setup = Join-Path $WORK 'BlockBox-9.9.9-win64-setup.exe'
if (-not (Test-Path $setup)) { throw "未生成安装包：$setup" }

Write-Host '==== 2. 运行探针（模拟拖入安装包）===='
Push-Location $APP
& (Join-Path $APP 'BlockBoxWindows.exe') $setup
$probeExit = $LASTEXITCODE
Pop-Location
Write-Host "探针退出码：$probeExit"

Write-Host '==== 3. 等待安装完成并重启 ===='
$deadline = (Get-Date).AddSeconds(120)
while ((Get-Date) -lt $deadline) {
    if (Test-Path (Join-Path $APP 'relaunched.txt')) { break }
    Start-Sleep -Milliseconds 400
}

Write-Host '==== 4. 校验 ===='
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
Check '安装包装到了当前程序目录' $true $exeReplaced
Check '用户配置未被出厂值覆盖' 'USER-CONFIG' (Get-Content (Join-Path $APP 'BlockBox\config.ini') -Raw -ErrorAction SilentlyContinue).Trim()
Check '用户账户仍然在' 'USER-ACCOUNTS' (Get-Content (Join-Path $APP 'BlockBox\accounts.json') -Raw -ErrorAction SilentlyContinue).Trim()
Check '安装后已重新启动新版本' 'NEW-VERSION-STARTED' (Get-Content (Join-Path $APP 'relaunched.txt') -Raw -ErrorAction SilentlyContinue).Trim()

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host ("结果：失败 {0} 项 -> {1}" -f $failures.Count, ($failures -join ', '))
    Get-ChildItem $env:TEMP -Filter 'BlockBox-update-*' -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem $_.FullName -Filter '*.log' -ErrorAction SilentlyContinue | ForEach-Object { Write-Host ('--- ' + $_.FullName); Get-Content $_.FullName -Tail 20 } }
    exit 1
}
Write-Host '结果：全部通过'
exit 0
