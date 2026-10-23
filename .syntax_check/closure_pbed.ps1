# 用 nm 计算目标文件闭包，避免手工迭代链接报错
param(
    [string[]]$Seeds,
    [string]$OutList = 'D:\BlockBox\C-BlockBox\.syntax_check\probe_pbed_objs.txt'
)
$bd = 'D:\BlockBox\C-BlockBox\build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug'
$debugDir = Join-Path $bd 'debug'
$nm = 'D:\APP\Qt\Tools\mingw1310_64\bin\nm.exe'
$env:PATH = "D:\APP\Qt\Tools\mingw1310_64\bin;$env:PATH"

$allObjs = @(Get-ChildItem $debugDir -Filter *.o | Select-Object -ExpandProperty FullName)

$defined = @{}     # symbol -> object path (first wins)
$undefOf = @{}     # object path -> symbol[]
foreach ($o in $allObjs) {
    $defs = & $nm -C --defined-only $o 2>$null
    foreach ($line in $defs) {
        if ($line -match '^[0-9a-fA-F]+\s+[A-Za-z]\s+(.+)$') {
            $sym = $Matches[1].Trim()
            if (-not $defined.ContainsKey($sym)) { $defined[$sym] = $o }
        }
    }
    $us = & $nm -C -u $o 2>$null
    $list = New-Object System.Collections.Generic.List[string]
    foreach ($line in $us) {
        if ($line -match '^\s+U\s+(.+)$') { $list.Add($Matches[1].Trim()) }
    }
    $undefOf[$o] = $list
}
Write-Host "indexed objects=$($allObjs.Count) defined symbols=$($defined.Count)"

# BFS 闭包
$need = New-Object System.Collections.Generic.Queue[string]
foreach ($s in $Seeds) { $need.Enqueue((Resolve-Path $s).Path) }
$picked = [System.Collections.Generic.HashSet[string]]::new()
$missing = [System.Collections.Generic.HashSet[string]]::new()
while ($need.Count -gt 0) {
    $cur = $need.Dequeue()
    if (-not $picked.Add($cur)) { continue }
    if (-not $undefOf.ContainsKey($cur)) { continue }
    foreach ($sym in $undefOf[$cur]) {
        if ($defined.ContainsKey($sym)) {
            $prov = $defined[$sym]
            if (-not $picked.Contains($prov)) { $need.Enqueue($prov) }
        } else {
            [void]$missing.Add($sym)
        }
    }
}

$final = $picked | Where-Object { $_ -like "$debugDir*" } | Sort-Object
$final | Set-Content $OutList -Encoding ascii
Write-Host "closure objects=$($final.Count)"
Write-Host "--- unresolved (非 Qt 库符号, 前 30) ---"
$missing | Where-Object { $_ -notmatch '^(_?Z|_?_ZN|__|qt_|Q|std::|operator|typeinfo|vtable|_Unwind|memcpy|memset|strlen|malloc|free|abort|__cxa)' } | Select-Object -First 30 | ForEach-Object { Write-Host "MISS $_" }
Write-Host "--- 全部未解析符号数=$($missing.Count) ---"
