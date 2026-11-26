@echo off
rem 实例助手「开服」页探针：一键本地开服的准备 / 启动 / 停止全流程。
rem 探针要链接主程序 release 目标文件，所以先用 build-win-release 目标编译一次主程序。
setlocal enabledelayedexpansion
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
cd /d "%~dp0.."

set BUILDDIR=build-win-release
if not exist "%BUILDDIR%\release\object_script.BlockBoxWindows.Release" (
    echo [probe] object_script not found, build the project once first
    exit /b 1
)

if not exist .tmp_server_probe mkdir .tmp_server_probe
if not exist build-win-release\probe_out mkdir build-win-release\probe_out

echo === 1/3 编译假 Java（顶替 Minecraft 服务端） ===
g++.exe -O2 -static -o .tmp_server_probe\fake_server_java.exe tests\fake_server_java.cpp
if errorlevel 1 goto :fail

echo === 2/3 编译探针 ===
set QT=D:\APP\Qt\6.11.1\mingw_64
set DEF=-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN
set INC=-I. -Istyles -Ithird_party/cubiomes -I%QT%\include -I%QT%\include\QtSvg -I%QT%\include\QtOpenGLWidgets -I%QT%\include\QtOpenGL -I%QT%\include\QtWidgets -I%QT%\include\QtGui -I%QT%\include\QtNetwork -I%QT%\include\QtConcurrent -I%QT%\include\QtCore -I%QT%\mkspecs\win32-g++
g++.exe -c -pipe -O2 -fexceptions -mthreads %DEF% %INC% -o build-win-release\probe_out\instance_server_probe.o tests\instance_server_probe.cpp
if errorlevel 1 goto :fail

echo === 3/3 链接 ===
rem 目标文件列表统一加上 build-win-release/ 前缀：object_script 里的路径是相对该构建目录的
set RSP=build-win-release\probe_out\local_server_probe.rsp
if exist "%RSP%" del "%RSP%"
echo -Wl,-subsystem,console>> "%RSP%"
echo -mthreads>> "%RSP%"
echo -o>> "%RSP%"
echo build-win-release/probe_out/instance_server_probe.exe>> "%RSP%"
for /f "usebackq delims=" %%f in ("%BUILDDIR%\release\object_script.BlockBoxWindows.Release") do (
    set "entry=%%f"
    if "!entry!" NEQ "" (
        set "basename=!entry!"
        for %%p in ("!entry!") do set "basename=%%~nxp"
        set "key=!basename:.=_!"
        if /I not "!basename!"=="main.o" if /I not "!basename!"=="BlockBoxWindows_resource_res.o" (
            if not defined seen_!key! (
                set "seen_!key!=1"
                echo build-win-release/!entry!>> "%RSP%"
            )
        )
    )
)
echo build-win-release/probe_out/instance_server_probe.o>> "%RSP%"
echo build-win-release/release/BlockBoxWindows_resource_res.o>> "%RSP%"

rem 不同 qmake 版本的目标文件名不一样：优先找真正带 LIBS 的那份
set MK=Makefile.Release.Release
if not exist "%BUILDDIR%\%MK%" set MK=Makefile.Release
findstr /b /c:"LIBS" "%BUILDDIR%\%MK%" >nul 2>&1
if errorlevel 1 set MK=Makefile
for /f "tokens=2 delims==" %%a in ('findstr /b /c:"LIBS" "%BUILDDIR%\%MK%"') do set LIBS=%%a
set "LIBS=!LIBS:\=/!"
set "LIBS=!LIBS:release/BlockBoxWindows_resource_res.o=!"

g++.exe @%RSP% !LIBS!
if errorlevel 1 goto :fail

echo PROBE-BUILD-OK
echo 运行：set PATH=%QT%\bin;%%PATH%% ^&^& build-win-release\probe_out\instance_server_probe.exe
exit /b 0

:fail
echo PROBE-BUILD-FAILED
exit /b 1
