$ErrorActionPreference = 'Stop'
$p = 'D:\BlockBox\更新日志\方块盒子\beta5.md'
$lines = Get-Content -LiteralPath $p
$add = [ordered]@{}
$add['## 新增'] = @(
 '- 投影方块编辑器侧栏新增 7 个视角按钮（等轴、前、后、左、右、顶、底）和「聚焦」「截图」按钮，截图可以直接存成 PNG 图片。（DeepSeek）',
 '- 顶部工具栏新增「贴面放置」开关：打开后新方块会贴着鼠标所指的那一面放，不会和已有方块重叠。（DeepSeek）',
 '- 编辑器顶部的三个小标签会实时显示投影尺寸、当前可见面数和重建耗时，底部状态条显示鼠标指着的方块坐标。（DeepSeek）',
 '- 调色板和「当前方块」下拉框里每个方块都带上了方块小图标，一眼能看出是石头、玻璃还是木板。（DeepSeek）',
 '- 新增一批快捷键：数字 1~5 切换工具、Ctrl+Z / Ctrl+Y 撤销重做、F 聚焦、R 复位视角、Ctrl+S 保存、Ctrl+Shift+S 另存为、Ctrl+E 截图。（DeepSeek）'
)
$add['## 调整'] = @(
 '- 投影方块编辑器换成了卡片式布局：工具改成带图标的分段按钮，界面分成标题、工具、视图、侧栏和状态条，窗口默认更大，宽屏下能同时看到更多方块。（DeepSeek）',
 '- 视角操作从自由飞行改成更顺手的环绕方式：左键旋转、右键平移、滚轮缩放，可缩放的远近范围更大，转到接近正上方或正下方时画面也不会翻转。（DeepSeek）',
 '- 撤销和重做会在按钮提示里显示具体做了什么（如「撤销：放置方块」），最多可回退 80 步。（DeepSeek）',
 '- 拖动图层滑块时不再每次都把整个投影重新算一遍，滑动更跟手；图层按钮改成图标，不再被窗口边缘裁掉。（DeepSeek）'
)
$add['## 修复'] = @(
 '- 投影方块编辑器的撤销、重做以前点了没有任何反应，现在放置、破坏、替换、填充都能正常撤销和重做。（DeepSeek）',
 '- 鼠标在投影上移动不再卡顿：以前每动一下都要把整个投影扫一遍，现在只沿视线检测，再大的投影也能即时高亮鼠标指的方块。（DeepSeek）',
 '- 大投影的显示速度明显变快：看不见的内侧面会被自动跳过不画，渲染面数通常只剩原来的几分之一，重建也快了不少。（DeepSeek）',
 '- 方块边角有了自然的光影过渡，相邻方块之间会出现淡淡的阴影，立体感和游戏里更接近。（DeepSeek）',
 '- 修掉了编辑器侧栏右侧被裁掉的问题，视图预设的按钮现在能完整显示。（DeepSeek）',
 '- 「当前方块」默认改成石头，「放置」工具点下去马上能看到方块，不再因为默认是空气而像是没反应。（DeepSeek）'
)

$out = New-Object System.Collections.Generic.List[string]
$current = $null
foreach ($line in $lines) {
    if ($line -match '^## ') {
        if ($current -and $add.Contains($current)) {
            foreach ($x in $add[$current]) { $out.Add($x) }
            $add.Remove($current)
        }
        $current = $line.Trim()
    }
    $out.Add($line)
}
if ($current -and $add.Contains($current)) {
    foreach ($x in $add[$current]) { $out.Add($x) }
    $add.Remove($current)
}
foreach ($k in $add.Keys) {
    $out.Add('')
    $out.Add($k)
    foreach ($x in $add[$k]) { $out.Add($x) }
}
[System.IO.File]::WriteAllLines($p, $out, (New-Object System.Text.UTF8Encoding($false)))
Write-Output 'done'
