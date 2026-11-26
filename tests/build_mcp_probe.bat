@echo off
setlocal
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
cd /d D:\BlockBox\C-BlockBox\build-win-release

findstr /v /x /c:"release/main.o" release\object_script.BlockBoxWindows.Release > release\object_script.McpProbe.Release
if errorlevel 1 goto :fail

if not exist probe_out mkdir probe_out

g++ -c -pipe -O2 -Wall -fexceptions -mthreads ^
 -DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS ^
 -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG ^
 -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB ^
 -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN ^
 -I.. -I../styles -I../third_party/cubiomes ^
 -ID:/APP/Qt/6.11.1/mingw_64/include -ID:/APP/Qt/6.11.1/mingw_64/include/QtSvg ^
 -ID:/APP/Qt/6.11.1/mingw_64/include/QtOpenGLWidgets -ID:/APP/Qt/6.11.1/mingw_64/include/QtOpenGL ^
 -ID:/APP/Qt/6.11.1/mingw_64/include/QtWidgets -ID:/APP/Qt/6.11.1/mingw_64/include/QtGui ^
 -ID:/APP/Qt/6.11.1/mingw_64/include/QtNetwork -ID:/APP/Qt/6.11.1/mingw_64/include/QtConcurrent ^
 -ID:/APP/Qt/6.11.1/mingw_64/include/QtCore -ID:/APP/Qt/6.11.1/mingw_64/mkspecs/win32-g++ ^
 -o probe_out\mcp_server_probe.o ..\tests\mcp_server_probe.cpp
if errorlevel 1 goto :fail

for /f "tokens=2 delims==" %%a in ('findstr /b /c:"LIBS" Makefile.Release') do set LIBS=%%a

g++ -Wl,-subsystem,console -mthreads -o probe_out\mcp_server_probe.exe ^
 @release\object_script.McpProbe.Release probe_out\mcp_server_probe.o %LIBS%
if errorlevel 1 goto :fail

echo PROBE-BUILD-OK
exit /b 0

:fail
echo PROBE-BUILD-FAILED
exit /b 1
