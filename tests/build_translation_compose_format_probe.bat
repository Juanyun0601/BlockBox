@echo off
chcp 65001 >nul
rem 翻译页「详细信息」格式栏探针：用真实页面 + 真实 style.qss 渲染并验证格式按钮。
rem 复用 build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug\release 里已有的对象集，
rem 只重新编译 TranslationComposePage 与探针本身，输出 exe 到 .tmp_translation_format_probe。
rem 用法：在项目根目录执行 tests\build_translation_compose_format_probe.bat
setlocal enabledelayedexpansion
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
cd /d "%~dp0.."

set BUILDDIR=build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug
set OBJDIR=%BUILDDIR%\release
set OBJDIR=%OBJDIR:\=/%
set SCRIPT=%BUILDDIR%\release\object_script.BlockBoxWindows.Release
if not exist "%SCRIPT%" (
    echo [probe] object_script not found, build the project once first
    exit /b 1
)

if not exist .tmp_translation_format_probe mkdir .tmp_translation_format_probe

set QT=D:\APP\Qt\6.11.1\mingw_64
set DEF=-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DBLOCKBOX_WORLDMAP_REMOTE_MODULE -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN
set INC=-I. -Istyles -Ithird_party/cubiomes -I%QT%\include -I%QT%\include\QtSvg -I%QT%\include\QtOpenGLWidgets -I%QT%\include\QtOpenGL -I%QT%\include\QtWidgets -I%QT%\include\QtGui -I%QT%\include\QtNetwork -I%QT%\include\QtConcurrent -I%QT%\include\QtCore -I%QT%\mkspecs\win32-g++
set FLAGS=-c -pipe -fno-keep-inline-dllexport -O2 -Wall -Wextra -fexceptions -mthreads

echo === moc ===
%QT%\bin\moc.exe %DEF% %INC% -o .tmp_translation_format_probe\moc_TranslationComposePage.cpp pages\TranslationComposePage.h
if errorlevel 1 goto :fail

echo === compiling page ===
g++.exe %FLAGS% %DEF% %INC% -o .tmp_translation_format_probe\TranslationComposePage.o pages\TranslationComposePage.cpp
if errorlevel 1 goto :fail
g++.exe %FLAGS% %DEF% %INC% -o .tmp_translation_format_probe\moc_TranslationComposePage.o .tmp_translation_format_probe\moc_TranslationComposePage.cpp
if errorlevel 1 goto :fail

echo === compiling probe ===
g++.exe %FLAGS% %DEF% %INC% -o .tmp_translation_format_probe\translation_compose_format_probe.o tests\translation_compose_format_probe.cpp
if errorlevel 1 goto :fail

echo === collecting objects ===
set RSP=.tmp_translation_format_probe\link.rsp
if exist "%RSP%" del "%RSP%"
echo -Wl,-subsystem,console>> "%RSP%"
echo -mthreads>> "%RSP%"
echo -o>> "%RSP%"
echo .tmp_translation_format_probe/translation_format_probe.exe>> "%RSP%"
for /f "usebackq delims=" %%f in ("%SCRIPT%") do (
    set "entry=%%f"
    if "!entry!" NEQ "" (
        set "entry=!entry:release/=!"
        if /I not "!entry!"=="main.o" if /I not "!entry!"=="TranslationComposePage.o" if /I not "!entry!"=="moc_TranslationComposePage.o" echo %OBJDIR%/!entry!>> "%RSP%"
    )
)
echo .tmp_translation_format_probe/TranslationComposePage.o>> "%RSP%"
echo .tmp_translation_format_probe/moc_TranslationComposePage.o>> "%RSP%"
echo .tmp_translation_format_probe/translation_compose_format_probe.o>> "%RSP%"

for /f "tokens=2 delims==" %%a in ('findstr /b /c:"LIBS" %BUILDDIR%\Makefile.Release') do set LIBS=%%a
set "LIBS=!LIBS:\=/!"
set "RESOBJ=!OBJDIR!/BlockBoxWindows_resource_res.o"
set "LIBS=!LIBS:release/BlockBoxWindows_resource_res.o=@RES@!"
set "LIBS=!LIBS:@RES@=%RESOBJ%!"

echo === linking ===
g++.exe @%RSP% !LIBS!
if errorlevel 1 goto :fail

echo PROBE-BUILD-OK
echo run: .tmp_translation_format_probe\translation_format_probe.exe
exit /b 0

:fail
echo PROBE-BUILD-FAILED
exit /b 1
