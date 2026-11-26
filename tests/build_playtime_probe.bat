@echo off
rem 游玩统计 / 年度报告页样式探针：把两个新页面 + 分享海报单独编出来，
rem 链接主程序其余目标文件（跳过 main.o 与新页面自己的对象），跑完在
rem .tmp_playtime_probe\probe_out\ 下留截图。先正常构建一次主程序再跑本脚本。
setlocal enabledelayedexpansion
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
cd /d "%~dp0.."

set BUILDDIR=build-win-release
if not exist "%BUILDDIR%\release\object_script.BlockBoxWindows.Release" set BUILDDIR=.
set SCRIPT=%BUILDDIR%\release\object_script.BlockBoxWindows.Release
if not exist "%SCRIPT%" (
    echo [probe] object_script not found, build the project once first
    exit /b 1
)
set OBJDIR=%BUILDDIR%/release
set OUT=.tmp_playtime_probe
if not exist %OUT% mkdir %OUT%

set QT=D:\APP\Qt\6.11.1\mingw_64
set DEF=-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN
set INC=-I. -Istyles -Ithird_party/cubiomes -I%QT%\include -I%QT%\include\QtSvg -I%QT%\include\QtOpenGLWidgets -I%QT%\include\QtOpenGL -I%QT%\include\QtWidgets -I%QT%\include\QtGui -I%QT%\include\QtNetwork -I%QT%\include\QtConcurrent -I%QT%\include\QtCore -I%QT%\mkspecs\win32-g++
set CXXFLAGS=-c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads

echo === moc ===
%QT%\bin\moc.exe %DEF% %INC% -o %OUT%\moc_PlaytimeStats.cpp utils\PlaytimeStats.h
if errorlevel 1 goto :fail
%QT%\bin\moc.exe %DEF% %INC% -o %OUT%\moc_PlaytimeStatsPage.cpp pages\PlaytimeStatsPage.h
if errorlevel 1 goto :fail
%QT%\bin\moc.exe %DEF% %INC% -o %OUT%\moc_AnnualReportPage.cpp pages\AnnualReportPage.h
if errorlevel 1 goto :fail

echo === compiling sources ===
for %%f in (utils\PlaytimeStats.cpp pages\PlaytimeStatsPage.cpp pages\AnnualReportPage.cpp components\AnnualReportCard.cpp %OUT%\moc_PlaytimeStats.cpp %OUT%\moc_PlaytimeStatsPage.cpp %OUT%\moc_AnnualReportPage.cpp tests\playtime_page_probe.cpp) do (
    echo   compile %%f
    g++.exe %CXXFLAGS% %DEF% %INC% -o %OUT%\%%~nf.o %%f
    if errorlevel 1 goto :fail
)

echo === collecting objects ===
set RSP=%OUT%\link.rsp
if exist "%RSP%" del "%RSP%"
echo -Wl,-subsystem,console>> "%RSP%"
echo -mthreads>> "%RSP%"
echo -o>> "%RSP%"
echo %OUT%/playtime_page_probe.exe>> "%RSP%"
for /f "usebackq delims=" %%f in ("%SCRIPT%") do (
    set "entry=%%f"
    if "!entry!" NEQ "" (
        set "entry=!entry:release/=!"
        if /I not "!entry!"=="main.o" if /I not "!entry!"=="PlaytimeStats.o" if /I not "!entry!"=="moc_PlaytimeStats.o" if /I not "!entry!"=="PlaytimeStatsPage.o" if /I not "!entry!"=="moc_PlaytimeStatsPage.o" if /I not "!entry!"=="AnnualReportPage.o" if /I not "!entry!"=="moc_AnnualReportPage.o" if /I not "!entry!"=="AnnualReportCard.o" if exist "%OBJDIR%\!entry!" echo !OBJDIR!/!entry!>> "%RSP%"
    )
)
for %%f in (utils\PlaytimeStats.cpp pages\PlaytimeStatsPage.cpp pages\AnnualReportPage.cpp components\AnnualReportCard.cpp %OUT%\moc_PlaytimeStats.cpp %OUT%\moc_PlaytimeStatsPage.cpp %OUT%\moc_AnnualReportPage.cpp tests\playtime_page_probe.cpp) do (
    echo %OUT%/%%~nf.o>> "%RSP%"
)

for /f "tokens=2 delims==" %%a in ('findstr /b /c:"LIBS" %BUILDDIR%\Makefile.Release') do set LIBS=%%a
set "LIBS=!LIBS:\=/!"
set "RESOBJ=!OBJDIR!/BlockBoxWindows_resource_res.o"
set "LIBS=!LIBS:release/BlockBoxWindows_resource_res.o=@RES@!"
set "LIBS=!LIBS:@RES@=%RESOBJ%!"

echo === linking ===
g++.exe @%RSP% !LIBS!
if errorlevel 1 goto :fail

echo PROBE-BUILD-OK
echo run: %OUT%\playtime_page_probe.exe
exit /b 0

:fail
echo PROBE-BUILD-FAILED
exit /b 1
