@echo off
set PATH=D:\APP\Qt\Tools\mingw1310_64\bin;%PATH%
cd /d D:\BlockBox\C-BlockBox
echo before > .syntax_check\bat_dbg.txt
D:\APP\Qt\Tools\mingw1310_64\bin\g++.exe -c .syntax_check\hello_test.cpp -o .syntax_check\hello_test.o
echo after g++ errorlevel=%ERRORLEVEL% >> .syntax_check\bat_dbg.txt
dir .syntax_check\hello_test.o >> .syntax_check\bat_dbg.txt 2>&1
g++ -v >> .syntax_check\bat_dbg.txt 2>&1
