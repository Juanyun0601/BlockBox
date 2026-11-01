@echo off
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;%PATH%
cd /d D:\BlockBox\C-BlockBox
set FLAGS=-fno-keep-inline-dllexport -O1 -Wall -Wextra -fexceptions -mthreads -DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_GUI_LIB -DQT_CORE_LIB -I. -Istyles -ID:\APP\Qt\6.11.1\mingw_64\include -ID:\APP\Qt\6.11.1\mingw_64\include\QtGui -ID:\APP\Qt\6.11.1\mingw_64\include\QtCore -ID:\APP\Qt\6.11.1\mingw_64\mkspecs\win32-g++
set GPP=D:\APP\Qt\Tools\mingw1310_64\bin\g++.exe
%GPP% -c .syntax_check\probe_loot_icons.cpp -o .syntax_check\probe_loot_icons.o %FLAGS%
if errorlevel 1 goto fail
%GPP% -c utils\Schematic\InstanceTextureLoader.cpp -o .syntax_check\itl_probe.o %FLAGS%
if errorlevel 1 goto fail
%GPP% -c utils\JarUtils.cpp -o .syntax_check\jarutils_probe.o %FLAGS%
if errorlevel 1 goto fail
%GPP% -Wl,--subsystem,console -mthreads -o .syntax_check\probe_loot_icons.exe .syntax_check\probe_loot_icons.o .syntax_check\itl_probe.o .syntax_check\jarutils_probe.o D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Gui.a D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Core.a -lz -lmingw32 -lpdh -liphlpapi -ldwmapi -lgdi32 -lole32 -lshell32 -lshlwapi -luuid -lcomdlg32 -luser32 -ladvapi32 -lws2_32 -loleaut32
if errorlevel 1 goto fail
echo BUILD OK
exit /b 0
:fail
echo BUILD FAIL
exit /b 1
