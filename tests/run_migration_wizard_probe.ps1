# 迁移向导探针包装脚本：备份 / 恢复真实设置后运行
# 用法：pwsh -File tests\run_migration_wizard_probe.ps1
$ErrorActionPreference = 'Stop'

$backupDir = 'D:\BlockBox\_out\migration_probe'
$backupReg = Join-Path $backupDir 'settings_backup.reg'
$logPath = Join-Path $backupDir 'probe_run.log'
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

$key = 'HKCU\Software\BlockBox'
$hadSettings = $false

Write-Host '=== backup settings ==='
& cmd /c "reg query `"$key`" >nul 2>&1"
if ($LASTEXITCODE -eq 0) {
    & cmd /c "reg export `"$key`" `"$backupReg`" /y" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'reg export failed' }
    $hadSettings = $true
    Write-Host "backed up to $backupReg"
} else {
    Write-Host 'no existing settings key'
}

try {
    # 探针的数据目录跟随 exe：每轮先清掉，避免上一轮导入的账户 / 文件夹影响结果
    Remove-Item -Recurse -Force 'D:\BlockBox\C-BlockBox\.tmp_migration_probe\BlockBox' -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force 'D:\BlockBox\C-BlockBox\.tmp_migration_probe\.minecraft' -ErrorAction SilentlyContinue
    $env:PATH = 'D:\APP\Qt\6.11.1\mingw_64\bin;' + $env:PATH
    $exe = 'D:\BlockBox\C-BlockBox\.tmp_migration_probe\migration_wizard_probe.exe'
    if (-not (Test-Path $exe)) { throw "probe exe not found: $exe" }
    & $exe 2>&1 | Tee-Object -FilePath $logPath
    $probeExit = $LASTEXITCODE
} finally {
    Write-Host '=== restore settings ==='
    & cmd /c "reg delete `"$key`" /f >nul 2>&1" | Out-Null
    if ($hadSettings) {
        & cmd /c "reg import `"$backupReg`" >nul 2>&1" | Out-Null
        if ($LASTEXITCODE -eq 0) {
            # 恢复成功就不再保留用户设置的副本
            Remove-Item -Force $backupReg -ErrorAction SilentlyContinue
        }
        Write-Host 'settings restored'
    } else {
        Write-Host 'settings cleaned'
    }
    Remove-Item -Recurse -Force 'D:\BlockBox\C-BlockBox\.tmp_migration_probe\.minecraft' -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force 'D:\BlockBox\C-BlockBox\.tmp_migration_probe\BlockBox' -ErrorAction SilentlyContinue
}

exit $probeExit
