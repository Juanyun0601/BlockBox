# 集成验证：真实启动器进程 + 本地 MCP 服务器（跑完自动清理注册表改动）
$ErrorActionPreference = 'Stop'
$regPath = 'HKCU:\Software\BlockBox\BlockBox\mcp'
$hadKey = Test-Path $regPath
$backup = $null
if ($hadKey) {
    $backup = Get-ItemProperty -Path $regPath
}

$port = 17890
$token = 'integration-probe-token'
New-Item -Path $regPath -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'enabled' -PropertyType DWord -Value 1 -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'port' -PropertyType DWord -Value $port -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'token' -PropertyType String -Value $token -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'allowWrite' -PropertyType DWord -Value 0 -Force | Out-Null

$env:PATH = 'D:\APP\Qt\6.11.1\mingw_64\bin;D:\APP\Qt\Tools\mingw1310_64\bin;' + $env:PATH
$env:QT_PLUGIN_PATH = 'D:\APP\Qt\6.11.1\mingw_64\plugins'
$exe = 'D:\BlockBox\C-BlockBox\build-win-release\release\BlockBoxWindows.exe'
$proc = Start-Process -FilePath $exe -WorkingDirectory 'D:\BlockBox\C-BlockBox\build-win-release\release' -PassThru
Write-Output "started pid=$($proc.Id)"

try {
    $ok = $false
    for ($i = 0; $i -lt 90; $i++) {
        Start-Sleep -Milliseconds 700
        try {
            $health = Invoke-RestMethod -Uri "http://127.0.0.1:$port/health" -TimeoutSec 2 -ErrorAction Stop
            if ($health.ok) { $ok = $true; Write-Output "health: name=$($health.name) version=$($health.version) tools=$($health.tools) write=$($health.write)"; break }
        } catch { }
    }
    if (-not $ok) { throw 'MCP /health 未就绪' }

    $headers = @{ Authorization = "Bearer $token"; 'Content-Type' = 'application/json' }
    $initBody = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"integration","version":"1.0"}}}'
    $init = Invoke-RestMethod -Uri "http://127.0.0.1:$port/mcp" -Method Post -Headers $headers -Body $initBody -TimeoutSec 10
    Write-Output "initialize: server=$($init.result.serverInfo.name) version=$($init.result.protocolVersion)"

    $listBody = '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
    $list = Invoke-RestMethod -Uri "http://127.0.0.1:$port/mcp" -Method Post -Headers $headers -Body $listBody -TimeoutSec 10
    Write-Output "tools/list: count=$($list.result.tools.Count) names=$((($list.result.tools | ForEach-Object { $_.name }) -join ','))"

    $callBody = '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_instances","arguments":{}}}'
    $call = Invoke-RestMethod -Uri "http://127.0.0.1:$port/mcp" -Method Post -Headers $headers -Body $callBody -TimeoutSec 30
    Write-Output "tools/call list_instances: isError=$($call.result.isError) textLen=$($call.result.content[0].text.Length)"
    Write-Output ("text head: " + $call.result.content[0].text.Substring(0, [Math]::Min(220, $call.result.content[0].text.Length)))

    $unauth = $null
    try {
        Invoke-RestMethod -Uri "http://127.0.0.1:$port/mcp" -Method Post -Headers @{ 'Content-Type' = 'application/json' } -Body $listBody -TimeoutSec 5 | Out-Null
        Write-Output 'unauth: UNEXPECTED 200'
    } catch {
        Write-Output "unauth: rejected status=$([int]$_.Exception.Response.StatusCode)"
    }

    Write-Output 'INTEGRATION-OK'
}
finally {
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue; $proc.WaitForExit(5000) | Out-Null }
    if ($hadKey) {
        Get-ItemProperty -Path $regPath -ErrorAction SilentlyContinue | Out-Null
        Remove-ItemProperty -Path $regPath -Name 'enabled','port','token','allowWrite' -ErrorAction SilentlyContinue
        if ($backup) {
            foreach ($name in 'enabled','port','token','allowWrite') {
                if ($null -ne $backup.$name) {
                    New-ItemProperty -Path $regPath -Name $name -Value $backup.$name -Force | Out-Null
                }
            }
        }
    } else {
        Remove-Item -Path $regPath -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Output 'cleanup done'
}
