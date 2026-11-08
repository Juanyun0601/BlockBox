@echo off
rem Multiplayer page style probe. Builds pages\MultiplayerPage.cpp + its moc
rem together with tests\multiplayer_page_probe.cpp into a standalone exe, linking
rem the object set from release\object_script.BlockBoxWindows.Release (main.o and
rem the old page objects are skipped). Run it after a normal build; it writes
rem .tmp_mp_probe\probe_out\mp_*.png.
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

if not exist .tmp_mp_probe mkdir .tmp_mp_probe

set QT=D:\APP\Qt\6.11.1\mingw_64
set DEF=-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN
set INC=-I. -Istyles -Ithird_party/cubiomes -I%QT%\include -I%QT%\include\QtSvg -I%QT%\include\QtOpenGLWidgets -I%QT%\include\QtOpenGL -I%QT%\include\QtWidgets -I%QT%\include\QtGui -I%QT%\include\QtNetwork -I%QT%\include\QtConcurrent -I%QT%\include\QtCore -I%QT%\mkspecs\win32-g++

echo === moc ===
%QT%\bin\moc.exe %DEF% %INC% -o .tmp_mp_probe\moc_MultiplayerPage.cpp pages\MultiplayerPage.h
if errorlevel 1 goto :fail

echo === compiling page ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_mp_probe\MultiplayerPage_probe.o pages\MultiplayerPage.cpp
if errorlevel 1 goto :fail

echo === compiling moc ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_mp_probe\moc_MultiplayerPage_probe.o .tmp_mp_probe\moc_MultiplayerPage.cpp
if errorlevel 1 goto :fail

echo === compiling probe ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_mp_probe\multiplayer_page_probe.o tests\multiplayer_page_probe.cpp
if errorlevel 1 goto :fail

echo === collecting objects ===
set RSP=.tmp_mp_probe\link.rsp
if exist "%RSP%" del "%RSP%"
echo -Wl,-subsystem,console>> "%RSP%"
echo -mthreads>> "%RSP%"
echo -o>> "%RSP%"
echo .tmp_mp_probe/multiplayer_page_probe.exe>> "%RSP%"
for /f "usebackq delims=" %%f in ("%SCRIPT%") do (
    set "entry=%%f"
    if "!entry!" NEQ "" (
        set "entry=!entry:release/=!"
        if /I not "!entry!"=="main.o" if /I not "!entry!"=="MultiplayerPage.o" if /I not "!entry!"=="moc_MultiplayerPage.o" echo !OBJDIR!/!entry!>> "%RSP%"
    )
)
echo .tmp_mp_probe/MultiplayerPage_probe.o>> "%RSP%"
echo .tmp_mp_probe/moc_MultiplayerPage_probe.o>> "%RSP%"
echo .tmp_mp_probe/multiplayer_page_probe.o>> "%RSP%"

for /f "tokens=2 delims==" %%a in ('findstr /b /c:"LIBS" %BUILDDIR%\Makefile.Release') do set LIBS=%%a
set "LIBS=!LIBS:\=/!"
set "RESOBJ=!OBJDIR!/BlockBoxWindows_resource_res.o"
set "LIBS=!LIBS:release/BlockBoxWindows_resource_res.o=@RES@!"
set "LIBS=!LIBS:@RES@=%RESOBJ%!"

echo === linking ===
g++.exe @%RSP% !LIBS!
if errorlevel 1 goto :fail

echo PROBE-BUILD-OK
echo run: .tmp_mp_probe\multiplayer_page_probe.exe
exit /b 0

:fail
echo PROBE-BUILD-FAILED
exit /b 1
