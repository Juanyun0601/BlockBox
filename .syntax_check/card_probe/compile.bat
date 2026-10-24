@echo off
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;%PATH%
cd /d D:\BlockBox\C-BlockBox
if not exist .syntax_check\card_probe\instance\probe1 mkdir .syntax_check\card_probe\instance\probe1
if not exist .syntax_check\card_probe\instance\probe1\mods mkdir .syntax_check\card_probe\instance\probe1\mods
> .syntax_check\card_probe\instance\probe1\probe1.json echo {"id":"probe1","clientVersion":"1.21.1","inheritsFrom":"1.21.1"}
D:\APP\Qt\Tools\mingw1310_64\bin\g++.exe -c .syntax_check\card_probe\main.cpp -o .syntax_check\card_probe\main_probe.o -fno-keep-inline-dllexport -O1 -Wall -fexceptions -mthreads -DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB -DQT_NEEDS_QMAIN -I. -Istyles -ID:\APP\Qt\6.11.1\mingw_64\include -ID:\APP\Qt\6.11.1\mingw_64\include\QtSvg -ID:\APP\Qt\6.11.1\mingw_64\include\QtOpenGLWidgets -ID:\APP\Qt\6.11.1\mingw_64\include\QtOpenGL -ID:\APP\Qt\6.11.1\mingw_64\include\QtWidgets -ID:\APP\Qt\6.11.1\mingw_64\include\QtGui -ID:\APP\Qt\6.11.1\mingw_64\include\QtNetwork -ID:\APP\Qt\6.11.1\mingw_64\include\QtConcurrent -ID:\APP\Qt\6.11.1\mingw_64\include\QtCore -ID:\APP\Qt\6.11.1\mingw_64\mkspecs\win32-g++
echo COMPILE_EXIT=%ERRORLEVEL%
