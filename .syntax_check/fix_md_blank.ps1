$ErrorActionPreference = 'Stop'
$p = 'D:\BlockBox\更新日志\方块盒子\beta5.md'
$lines = Get-Content -LiteralPath $p
$out = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    if ($line -match '^## ' -and $out.Count -gt 0 -and $out[$out.Count - 1].Trim() -ne '') {
        $out.Add('')
    }
    $out.Add($line)
}
[System.IO.File]::WriteAllLines($p, $out, (New-Object System.Text.UTF8Encoding($false)))
Write-Output 'ok'
