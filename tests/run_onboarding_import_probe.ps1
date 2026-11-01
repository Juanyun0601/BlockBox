# 新手引导「从启动器导入」页交互探针的包装脚本：备份/恢复真实设置后跑 Git Bash 探针
# 用法：pwsh -File tests\run_onboarding_import_probe.ps1
$ErrorActionPreference = 'Stop'

$backupDir = 'D:\BlockBox\_out\onboarding_probe'
$backupReg = Join-Path $backupDir 'settings_backup.reg'
$logPath   = Join-Path $backupDir 'probe_run.log'
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
    $bash = 'C:\Program Files\Git\bin\bash.exe'
    & $bash -lc "bash /d/BlockBox/C-BlockBox/tests/run_onboarding_import_probe.sh" 2>&1 |
        Tee-Object -FilePath $logPath
    $probeExit = $LASTEXITCODE
} finally {
    Write-Host '=== restore settings ==='
    & cmd /c "reg delete `"$key`" /f >nul 2>&1" | Out-Null
    if ($hadSettings) {
        & cmd /c "reg import `"$backupReg`" >nul 2>&1" | Out-Null
        Write-Host 'settings restored'
    } else {
        Write-Host 'settings cleaned'
    }
}

exit $probeExit
