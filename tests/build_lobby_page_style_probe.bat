@echo off
rem Lobby page style probe. Builds pages\MultiplayerLobbyPage.cpp and
rem pages\MultiplayerLobbyDetailPage.cpp together with tests\lobby_page_style_probe.cpp
rem into a standalone exe, linking the object set listed in
rem release\object_script.BlockBoxWindows.Release (main.o and the old page objects are
rem skipped). Run it after a normal build; it writes probe_out\lobby_*.png.
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

if not exist .tmp_lobby_probe mkdir .tmp_lobby_probe

set QT=D:\APP\Qt\6.11.1\mingw_64
set DEF=-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN
set INC=-I. -Istyles -Ithird_party/cubiomes -I%QT%\include -I%QT%\include\QtSvg -I%QT%\include\QtOpenGLWidgets -I%QT%\include\QtOpenGL -I%QT%\include\QtWidgets -I%QT%\include\QtGui -I%QT%\include\QtNetwork -I%QT%\include\QtConcurrent -I%QT%\include\QtCore -I%QT%\mkspecs\win32-g++

echo === moc ===
%QT%\bin\moc.exe %DEF% %INC% -o .tmp_lobby_probe\moc_MultiplayerLobbyDetailPage.cpp pages\MultiplayerLobbyDetailPage.h
if errorlevel 1 goto :fail
%QT%\bin\moc.exe %DEF% %INC% -o .tmp_lobby_probe\moc_ForumKit.cpp components\ForumKit.h
if errorlevel 1 goto :fail

echo === compiling ForumKit ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_lobby_probe\ForumKit_probe.o components\ForumKit.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_lobby_probe\moc_ForumKit_probe.o .tmp_lobby_probe\moc_ForumKit.cpp
if errorlevel 1 goto :fail

echo === compiling MasonryContentCard ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_lobby_probe\MasonryContentCard_probe.o components\MasonryContentCard.cpp
if errorlevel 1 goto :fail

echo === compiling detail page ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_lobby_probe\MultiplayerLobbyDetailPage_probe.o pages\MultiplayerLobbyDetailPage.cpp
if errorlevel 1 goto :fail

echo === compiling detail moc ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_lobby_probe\moc_MultiplayerLobbyDetailPage_probe.o .tmp_lobby_probe\moc_MultiplayerLobbyDetailPage.cpp
if errorlevel 1 goto :fail

echo === compiling page ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_lobby_probe\MultiplayerLobbyPage_probe.o pages\MultiplayerLobbyPage.cpp
if errorlevel 1 goto :fail

echo === compiling probe ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_lobby_probe\lobby_page_style_probe.o tests\lobby_page_style_probe.cpp
if errorlevel 1 goto :fail

echo === collecting objects ===
set RSP=.tmp_lobby_probe\link.rsp
if exist "%RSP%" del "%RSP%"
echo -Wl,-subsystem,console>> "%RSP%"
echo -mthreads>> "%RSP%"
echo -o>> "%RSP%"
echo .tmp_lobby_probe/lobby_page_style_probe.exe>> "%RSP%"
for /f "usebackq delims=" %%f in ("%SCRIPT%") do (
    set "entry=%%f"
    if "!entry!" NEQ "" (
        set "entry=!entry:release/=!"
        if /I not "!entry!"=="main.o" if /I not "!entry!"=="MultiplayerLobbyPage.o" if /I not "!entry!"=="MasonryContentCard.o" if /I not "!entry!"=="MultiplayerLobbyDetailPage.o" if /I not "!entry!"=="moc_MultiplayerLobbyDetailPage.o" if /I not "!entry!"=="ForumKit.o" if /I not "!entry!"=="moc_ForumKit.o" echo !OBJDIR!/!entry!>> "%RSP%"
    )
)
echo .tmp_lobby_probe/ForumKit_probe.o>> "%RSP%"
echo .tmp_lobby_probe/moc_ForumKit_probe.o>> "%RSP%"
echo .tmp_lobby_probe/MasonryContentCard_probe.o>> "%RSP%"
echo .tmp_lobby_probe/MultiplayerLobbyDetailPage_probe.o>> "%RSP%"
echo .tmp_lobby_probe/moc_MultiplayerLobbyDetailPage_probe.o>> "%RSP%"
echo .tmp_lobby_probe/MultiplayerLobbyPage_probe.o>> "%RSP%"
echo .tmp_lobby_probe/lobby_page_style_probe.o>> "%RSP%"

for /f "tokens=2 delims==" %%a in ('findstr /b /c:"LIBS" %BUILDDIR%\Makefile.Release') do set LIBS=%%a
set "LIBS=!LIBS:\=/!"
set "RESOBJ=!OBJDIR!/BlockBoxWindows_resource_res.o"
set "LIBS=!LIBS:release/BlockBoxWindows_resource_res.o=@RES@!"
set "LIBS=!LIBS:@RES@=%RESOBJ%!"

echo === linking ===
g++.exe @%RSP% !LIBS!
if errorlevel 1 goto :fail

echo PROBE-BUILD-OK
echo run: .tmp_lobby_probe\lobby_page_style_probe.exe
exit /b 0

:fail
echo PROBE-BUILD-FAILED
exit /b 1
