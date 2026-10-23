@echo off
cd /d D:\BlockBox\C-BlockBox\build-ohos-release
set "CXXC=D:\BlockBox\qt-ohos\script\work\ohos-sdk\15\native/llvm/bin/clang++"
set "DEFS=-D__MUSL__=1 -DOHOS_STL=c++_shared -DOHOS_PLATFORM=OHOS -DOHOS_SDK_NATIVE_VERSION_STR="5.0.3.135" -DOHOS_SDK_NATIVE_VERSION_MAJOR=5 -DOHOS_SDK_NATIVE_VERSION_MINOR=0 -DOHOS_SDK_NATIVE_VERSION_PATCH=3 -DOHOS_SDK_NATIVE_VERSION_REVISION=135 -DQT_DEPRECATED_WARNINGS -DHARMONY_OS -DNODE_ADDON_API_CPP_EXCEPTIONS -DQT_NO_DEBUG -DQT_SVG_LIB -DQT_OPENGLWIDGETS_LIB -DQT_OPENGL_LIB -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_NETWORK_LIB -DQT_CONCURRENT_LIB -DQT_CORE_LIB"
set "FLAGS=--sysroot=D:\BlockBox\qt-ohos\script\work\ohos-sdk\15\native/sysroot --target=aarch64-linux-ohos -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong -no-canonical-prefixes -fno-addrsig -Wa,--noexecstack -Wformat -Werror=format-security -fPIC -O2 -std=gnu++17 -Wall -Wextra -D_REENTRANT "
set "INCP=-I..\..\C-BlockBox -I. -I..\styles -I..\..\qt-ohos\script\work\qt6\qtbase\src\3rdparty\node-addon-api -I..\third_party\cubiomes -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtSvg -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtOpenGLWidgets -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtOpenGL -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtWidgets -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtGui -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtNetwork -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtConcurrent -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtCore\6.13.0 -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtCore\6.13.0\QtCore -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\include\QtCore -I. -I. -I..\..\qt-ohos\script\work\output\Qt6.5.6-ohos15-arm64-v8a\mkspecs\ohos-clang"
for %%F in (%*) do call :chk %%F
echo DONE
exit /b 0
:chk
echo ===== %1 =====
%CXXC% -fsyntax-only %FLAGS% %DEFS% %INCP% %1
echo EXIT=%ERRORLEVEL%
exit /b 0
