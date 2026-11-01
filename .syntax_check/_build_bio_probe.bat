@echo off
rem 简介 Markdown 探针：编译 tests\forum_account_probe.cpp 并链接除 main.o 之外的全部 release 对象
setlocal
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;D:\APP\Qt\6.11.1\mingw_64\bin;%PATH%
cd /d D:\BlockBox\C-BlockBox\build-win-release

for /f "tokens=2 delims==" %%a in ('findstr /b /c:"LIBS" Makefile.Release') do set LIBS=%%a
findstr /v /x /c:"release/main.o" release\object_script.BlockBoxWindows.Release > release\object_script.Probe.Release
if errorlevel 1 goto :fail

g++ -Wl,-subsystem,console -mthreads -o probe_out\forum_account_probe.exe ^
 @release\object_script.Probe.Release probe_out\forum_account_probe.o %LIBS%
if errorlevel 1 goto :fail

echo PROBE-LINK-OK
exit /b 0

:fail
echo PROBE-LINK-FAILED
exit /b 1
