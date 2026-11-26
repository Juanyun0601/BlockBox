# 新闻列表页「加载全部新闻」探针构建脚本（Windows + Qt MinGW）
#
# 用假数据源（fake/utils/news/NewsFeed.h）驱动真实 NewsPage，离屏验证：
#   - 首屏按列数展开整屏卡片，而不是一次建满上百张；
#   - 底部「加载更多」点一次追加一屏，全部展开后自动隐藏；
#   - 滑到底部自动再续一屏。
#
# 用法：在 C-BlockBox 目录下执行
#   pwsh tests/news_paging_probe/build_probe.ps1
# 然后运行：
#   $env:QT_QPA_PLATFORM='offscreen'; .\tests\news_paging_probe\build\news_paging_probe.exe

$ErrorActionPreference = 'Stop'

$qt = 'D:/APP/Qt/6.11.1/mingw_64'
$mingwBin = 'D:\APP\Qt\Tools\mingw1310_64\bin'
$env:Path = "$mingwBin;$($qt -replace '/', '\')\bin;" + $env:Path

$probe = 'tests/news_paging_probe'
$build = "$probe/build"
New-Item -ItemType Directory -Force -Path $build | Out-Null

$defines = '-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS ' +
           '-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG ' +
           '-DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB'
$includes = "-I$probe/fake -I. -I$probe -I$qt/include -I$qt/include/QtWidgets " +
            "-I$qt/include/QtGui -I$qt/include/QtCore -I$qt/mkspecs/win32-g++"
$flags = @('-std=c++17', '-O1', '-Wall', '-Wextra', '-fexceptions', '-mthreads') +
         $defines.Split(' ') + $includes.Split(' ')

# moc：NewsPage / NewsCard 是真实控件，NewsFeed 是探针替身
& "$($qt -replace '/', '\')\bin\moc.exe" "$probe/fake/utils/news/NewsFeed.h" -o "$build/moc_NewsFeed.cpp"
& "$($qt -replace '/', '\')\bin\moc.exe" 'components/NewsCard.h' -o "$build/moc_NewsCard.cpp"
& "$($qt -replace '/', '\')\bin\moc.exe" 'pages/NewsPage.h' -o "$build/moc_NewsPage.cpp"

$sources = @(
  "$probe/probe.cpp",
  'pages/NewsPage.cpp',
  'components/NewsCard.cpp',
  "$build/moc_NewsFeed.cpp",
  "$build/moc_NewsCard.cpp",
  "$build/moc_NewsPage.cpp"
)

$objects = @()
foreach ($src in $sources) {
  $obj = "$build/" + [IO.Path]::GetFileNameWithoutExtension($src) + '.o'
  & "$mingwBin\g++.exe" @flags -c $src -o $obj
  if ($LASTEXITCODE -ne 0) { throw "编译失败：$src" }
  $objects += $obj
}

& "$mingwBin\g++.exe" @objects '-o' "$build/news_paging_probe.exe" "-L$qt/lib" `
  '-lQt6Widgets' '-lQt6Gui' '-lQt6Core' '-Wl,-subsystem,console'
if ($LASTEXITCODE -ne 0) { throw '链接失败' }

Write-Host "已生成 $build/news_paging_probe.exe"
