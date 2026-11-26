@echo off
rem 聊天页输入坞探针：重新编译 ForumFriendsPage + 探针，链接 release 里的其余对象。
setlocal enabledelayedexpansion
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
cd /d "%~dp0.."

set QT=D:\APP\Qt\6.11.1\mingw_64
set DEF=-DUNICODE -D_UNICODE -DWIN32 -DMINGW_HAS_SECURE_API=1 -DQT_DEPRECATED_WARNINGS -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DWINDOWS_OS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB
set INC=-I. -Istyles -Ithird_party/cubiomes -I%QT%\include -I%QT%\include\QtSvg -I%QT%\include\QtOpenGLWidgets -I%QT%\include\QtOpenGL -I%QT%\include\QtWidgets -I%QT%\include\QtGui -I%QT%\include\QtNetwork -I%QT%\include\QtConcurrent -I%QT%\include\QtCore -I%QT%\mkspecs\win32-g++
set OUT=_tmp_chat_input_probe

echo === moc ===
%QT%\bin\moc.exe %DEF% %INC% -o %OUT%\moc_ForumFriendsPage.cpp pages\ForumFriendsPage.h
if errorlevel 1 goto :fail
%QT%\bin\moc.exe %DEF% %INC% -o %OUT%\moc_ChatPrefs.cpp utils\ChatPrefs.h
if errorlevel 1 goto :fail
%QT%\bin\moc.exe %DEF% %INC% -o %OUT%\moc_ChatSettingsDialog.cpp components\ChatSettingsDialog.h
if errorlevel 1 goto :fail
%QT%\bin\moc.exe %DEF% %INC% -o %OUT%\moc_ChatUiStore.cpp utils\ChatUiStore.h
if errorlevel 1 goto :fail

echo === compiling page ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\ForumFriendsPage.o pages\ForumFriendsPage.cpp
if errorlevel 1 goto :fail

echo === compiling extra deps ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\ChatPrefs.o utils\ChatPrefs.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\ChatSettingsDialog.o components\ChatSettingsDialog.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\ChatUiStore.o utils\ChatUiStore.cpp
if errorlevel 1 goto :fail

echo === compiling moc ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\moc_ForumFriendsPage.o %OUT%\moc_ForumFriendsPage.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\moc_ChatPrefs.o %OUT%\moc_ChatPrefs.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\moc_ChatSettingsDialog.o %OUT%\moc_ChatSettingsDialog.cpp
if errorlevel 1 goto :fail
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\moc_ChatUiStore.o %OUT%\moc_ChatUiStore.cpp
if errorlevel 1 goto :fail

echo === compiling probe ===
g++.exe -c -pipe -fno-keep-inline-dllexport -O2 -fexceptions -mthreads %DEF% %INC% -o %OUT%\chat_input_probe.o %OUT%\chat_input_probe.cpp
if errorlevel 1 goto :fail

echo === collecting objects ===
set RSP=%OUT%\link.rsp
if exist "%RSP%" del "%RSP%"
echo -o>> "%RSP%"
echo %OUT%/chat_input_probe.exe>> "%RSP%"
for /f "usebackq delims=" %%f in ("release\object_script.BlockBoxWindows.Release") do (
    set "entry=%%f"
    if "!entry!" NEQ "" (
        if /I not "!entry!"=="release/main.o" if /I not "!entry!"=="release/ForumFriendsPage.o" if /I not "!entry!"=="release/moc_ForumFriendsPage.o" if /I not "!entry!"=="release/ChatUiStore.o" if /I not "!entry!"=="release/moc_ChatUiStore.o" echo !entry!>> "%RSP%"
    )
)
echo %OUT%/ForumFriendsPage.o>> "%RSP%"
echo %OUT%/ChatPrefs.o>> "%RSP%"
echo %OUT%/ChatSettingsDialog.o>> "%RSP%"
echo %OUT%/ChatUiStore.o>> "%RSP%"
echo %OUT%/moc_ChatUiStore.o>> "%RSP%"
echo %OUT%/moc_ForumFriendsPage.o>> "%RSP%"
echo %OUT%/moc_ChatPrefs.o>> "%RSP%"
echo %OUT%/moc_ChatSettingsDialog.o>> "%RSP%"
echo %OUT%/chat_input_probe.o>> "%RSP%"

set LIBS=-L"%QT%\lib" -lQt6Svg -lQt6OpenGLWidgets -lQt6OpenGL -lQt6Widgets -lQt6Gui -lQt6Network -lQt6Concurrent -lQt6Core -lz -lpdh -liphlpapi -ldwmapi -lgdi32 -lole32 -lshell32 -lshlwapi -luuid -lcomdlg32 -luser32 -ladvapi32 -lws2_32 -loleaut32

echo === linking ===
g++.exe -Wl,-s -Wl,-subsystem,console -mthreads @%RSP% %LIBS%
if errorlevel 1 goto :fail

echo PROBE-BUILD-OK
exit /b 0

:fail
echo PROBE-BUILD-FAILED
exit /b 1
