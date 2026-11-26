# 反复尝试构建并运行探针：别的会话可能正在改代码/编译，对象文件暂时不一致
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$deadline = (Get-Date).AddMinutes(30)
$attempt = 0
while ((Get-Date) -lt $deadline) {
    $attempt++
    $log = ".tmp_ai_sidebar_probe/build_attempt.log"
    $out = & pwsh -NoProfile -File tests\build_ai_sidebar_probe.ps1 2>&1
    $out | Set-Content $log -Encoding UTF8
    if ($out -match 'PROBE-BUILD-OK') {
        "PROBE-BUILD-OK on attempt $attempt"
        $env:PATH = 'D:\APP\Qt\6.11.1\mingw_64\bin;D:\APP\Qt\Tools\mingw1310_64\bin;' + $env:PATH
        & .\.tmp_ai_sidebar_probe\probe.exe 2>&1 | Select-Object -Last 2
        exit 0
    }
    "attempt $attempt failed: " + (($out | Select-String -Pattern 'error|undefined reference' | Select-Object -First 2) -join ' | ')
    Start-Sleep -Seconds 60
}
"giving up after $attempt attempts"
exit 1
