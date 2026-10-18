@echo off
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
cd /d D:\BlockBox\C-BlockBox\build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug
g++ -Wl,-subsystem,windows -mthreads -o debug\BlockBoxWindows.exe @debug\object_script.BlockBoxWindows.Debug -lz -lpdh -liphlpapi -ldwmapi -lgdi32 -lole32 -lshell32 -lshlwapi -luuid -lcomdlg32 -luser32 -ladvapi32 -lws2_32 -loleaut32 D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Svg.a D:\APP\Qt\6.11.1\mingw_64\lib\libQt6OpenGLWidgets.a D:\APP\Qt\6.11.1\mingw_64\lib\libQt6OpenGL.a D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Widgets.a D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Gui.a D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Network.a D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Concurrent.a D:\APP\Qt\6.11.1\mingw_64\lib\libQt6Core.a debug\BlockBoxWindows_resource_res.o -lmingw32 D:\APP\Qt\6.11.1\mingw_64\lib\libQt6EntryPoint.a -lshell32
echo EXIT=%ERRORLEVEL%
dir debug\BlockBoxWindows.exe
