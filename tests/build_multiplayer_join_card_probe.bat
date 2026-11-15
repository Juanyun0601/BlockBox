@echo off
rem Join-room inline card probe: builds both multiplayer pages plus the probe,
rem links them against the existing release objects. Produces .tmp_mp_join_probe\join_card_probe.exe
rem Prereq: build the project once so release objects exist.
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
set OUT=.tmp_mp_join_probe
if not exist %OUT% mkdir %OUT%

set QT=D:\APP\Qt\6.11.1\mingw_64
set DEF=-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN
set INC=-I. -Istyles -Ithird_party/cubiomes -I%QT%\include -I%QT%\include\QtSvg -I%QT%\include\QtOpenGLWidgets -I%QT%\include\QtOpenGL -I%QT%\include\QtWidgets -I%QT%\include\QtGui -I%QT%\include\QtNetwork -I%QT%\include\QtConcurrent -I%QT%\include\QtCore -I%QT%\mkspecs\win32-g++

echo === moc ===
%QT%\bin\moc.exe %DEF% %INC% -o %OUT%\moc_MultiplayerPage.cpp pages\MultiplayerPage.h
if errorlevel 1 goto :fail
%QT%\bin\moc.exe %DEF% %INC% -o %OUT%\moc_BedrockMultiplayerPage.cpp pages\BedrockMultiplayerPage.h
if errorlevel 1 goto :fail

echo === compiling pages ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\MultiplayerPage_probe.o pages\MultiplayerPage.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\BedrockMultiplayerPage_probe.o pages\BedrockMultiplayerPage.cpp
if errorlevel 1 goto :fail

echo === compiling services ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\ForumAccountService_probe.o utils\ForumAccountService.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\TerracottaClient_probe.o utils\Terracotta\TerracottaClient.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\GravityConeClient_probe.o utils\GravityCone\GravityConeClient.cpp
if errorlevel 1 goto :fail

echo === compiling mocs and probe ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\moc_MultiplayerPage_probe.o %OUT%\moc_MultiplayerPage.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\moc_BedrockMultiplayerPage_probe.o %OUT%\moc_BedrockMultiplayerPage.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\join_card_probe.o tests\multiplayer_join_card_probe.cpp
if errorlevel 1 goto :fail

echo === collecting objects ===
set RSP=%OUT%\link.rsp
if exist "%RSP%" del "%RSP%"
echo -Wl,-subsystem,console>> "%RSP%"
echo -mthreads>> "%RSP%"
echo -o>> "%RSP%"
echo %OUT%/join_card_probe.exe>> "%RSP%"
for /f "usebackq delims=" %%f in ("%SCRIPT%") do (
    set "entry=%%f"
    if "!entry!" NEQ "" (
        set "entry=!entry:release/=!"
        if /I not "!entry!"=="main.o" if /I not "!entry!"=="MultiplayerPage.o" if /I not "!entry!"=="moc_MultiplayerPage.o" if /I not "!entry!"=="BedrockMultiplayerPage.o" if /I not "!entry!"=="moc_BedrockMultiplayerPage.o" if /I not "!entry!"=="ForumAccountService.o" if /I not "!entry!"=="TerracottaClient.o" if /I not "!entry!"=="GravityConeClient.o" echo !OBJDIR!/!entry!>> "%RSP%"
    )
)
echo %OUT%/MultiplayerPage_probe.o>> "%RSP%"
echo %OUT%/BedrockMultiplayerPage_probe.o>> "%RSP%"
echo %OUT%/ForumAccountService_probe.o>> "%RSP%"
echo %OUT%/TerracottaClient_probe.o>> "%RSP%"
echo %OUT%/GravityConeClient_probe.o>> "%RSP%"
echo %OUT%/moc_MultiplayerPage_probe.o>> "%RSP%"
echo %OUT%/moc_BedrockMultiplayerPage_probe.o>> "%RSP%"
echo %OUT%/join_card_probe.o>> "%RSP%"

for /f "tokens=2 delims==" %%a in ('findstr /b /c:"LIBS" %BUILDDIR%\Makefile.Release') do set LIBS=%%a
set "LIBS=!LIBS:\=/!"
set "RESOBJ=!OBJDIR!/BlockBoxWindows_resource_res.o"
set "LIBS=!LIBS:release/BlockBoxWindows_resource_res.o=@RES@!"
set "LIBS=!LIBS:@RES@=%RESOBJ%!"

echo === linking ===
g++.exe @%RSP% !LIBS!
if errorlevel 1 goto :fail

echo PROBE-BUILD-OK
echo run: set QT_QPA_PLATFORM=offscreen ^&^& %OUT%\join_card_probe.exe
exit /b 0

:fail
echo PROBE-BUILD-FAILED
exit /b 1