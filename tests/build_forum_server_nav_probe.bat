@echo off
rem 服务器导航板块「来源」设置探针：重新编译 ForumThreadListPage / ModrinthAPI /
rem 探针本身，再复用 build-win-release\release 里已有的对象集链接成独立 exe。
rem 跑一遍正常构建之后执行：tests\build_forum_server_nav_probe.bat
setlocal enabledelayedexpansion
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
cd /d "%~dp0.."

set BUILDDIR=build-win-release
set SCRIPT=%BUILDDIR%\release\object_script.BlockBoxWindows.Release
if not exist "%SCRIPT%" (
    echo [probe] object_script not found, build the project once first
    exit /b 1
)
set OBJDIR=%BUILDDIR%/release

if not exist .tmp_forum_nav_probe mkdir .tmp_forum_nav_probe

set QT=D:\APP\Qt\6.11.1\mingw_64
set DEF=-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN
set INC=-I. -Istyles -Ithird_party/cubiomes -I%QT%\include -I%QT%\include\QtSvg -I%QT%\include\QtOpenGLWidgets -I%QT%\include\QtOpenGL -I%QT%\include\QtWidgets -I%QT%\include\QtGui -I%QT%\include\QtNetwork -I%QT%\include\QtConcurrent -I%QT%\include\QtCore -I%QT%\mkspecs\win32-g++

echo === moc ===
%QT%\bin\moc.exe %DEF% %INC% -o .tmp_forum_nav_probe\moc_ForumThreadListPage.cpp pages\ForumThreadListPage.h
if errorlevel 1 goto :fail
%QT%\bin\moc.exe %DEF% %INC% -o .tmp_forum_nav_probe\moc_ForumKit.cpp components\ForumKit.h
if errorlevel 1 goto :fail

echo === compiling forum kit ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_forum_nav_probe\ForumKit_probe.o components\ForumKit.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_forum_nav_probe\moc_ForumKit_probe.o .tmp_forum_nav_probe\moc_ForumKit.cpp
if errorlevel 1 goto :fail

echo === compiling page ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_forum_nav_probe\ForumThreadListPage_probe.o pages\ForumThreadListPage.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_forum_nav_probe\moc_ForumThreadListPage_probe.o .tmp_forum_nav_probe\moc_ForumThreadListPage.cpp
if errorlevel 1 goto :fail

echo === compiling modrinth api ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_forum_nav_probe\ModrinthAPI_probe.o utils\mod\ModrinthAPI.cpp
if errorlevel 1 goto :fail
%QT%\bin\moc.exe %DEF% %INC% -o .tmp_forum_nav_probe\moc_ModrinthAPI.cpp utils\mod\ModrinthAPI.h
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_forum_nav_probe\moc_ModrinthAPI_probe.o .tmp_forum_nav_probe\moc_ModrinthAPI.cpp
if errorlevel 1 goto :fail

echo === compiling probe ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o .tmp_forum_nav_probe\forum_server_nav_probe.o tests\forum_server_nav_probe.cpp
if errorlevel 1 goto :fail

echo === collecting objects ===
set RSP=.tmp_forum_nav_probe\link.rsp
if exist "%RSP%" del "%RSP%"
echo -Wl,-subsystem,console>> "%RSP%"
echo -mthreads>> "%RSP%"
echo -o>> "%RSP%"
echo .tmp_forum_nav_probe/forum_server_nav_probe.exe>> "%RSP%"
for /f "usebackq delims=" %%f in ("%SCRIPT%") do (
    set "entry=%%f"
    if "!entry!" NEQ "" (
        set "entry=!entry:release/=!"
        if /I not "!entry!"=="main.o" if /I not "!entry!"=="ForumThreadListPage.o" if /I not "!entry!"=="moc_ForumThreadListPage.o" if /I not "!entry!"=="ModrinthAPI.o" if /I not "!entry!"=="moc_ModrinthAPI.o" if /I not "!entry!"=="ForumKit.o" if /I not "!entry!"=="moc_ForumKit.o" echo !OBJDIR!/!entry!>> "%RSP%"
    )
)
echo .tmp_forum_nav_probe/ForumKit_probe.o>> "%RSP%"
echo .tmp_forum_nav_probe/moc_ForumKit_probe.o>> "%RSP%"
echo .tmp_forum_nav_probe/ForumThreadListPage_probe.o>> "%RSP%"
echo .tmp_forum_nav_probe/moc_ForumThreadListPage_probe.o>> "%RSP%"
echo .tmp_forum_nav_probe/ModrinthAPI_probe.o>> "%RSP%"
echo .tmp_forum_nav_probe/moc_ModrinthAPI_probe.o>> "%RSP%"
echo .tmp_forum_nav_probe/forum_server_nav_probe.o>> "%RSP%"

for /f "tokens=2 delims==" %%a in ('findstr /b /c:"LIBS" %BUILDDIR%\Makefile.Release') do set LIBS=%%a
set "LIBS=!LIBS:\=/!"
set "RESOBJ=!OBJDIR!/BlockBoxWindows_resource_res.o"
set "LIBS=!LIBS:release/BlockBoxWindows_resource_res.o=@RES@!"
set "LIBS=!LIBS:@RES@=%RESOBJ%!"

echo === linking ===
g++.exe @%RSP% !LIBS!
if errorlevel 1 goto :fail

echo PROBE-BUILD-OK
echo run: .tmp_forum_nav_probe\forum_server_nav_probe.exe
exit /b 0

:fail
echo PROBE-BUILD-FAILED
exit /b 1
