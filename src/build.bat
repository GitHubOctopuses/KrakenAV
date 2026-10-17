@echo off
rem ============================================================
rem  KrakenAV build script
rem  位置：<产品目录>\src\build.bat
rem  源码 = 本目录；工作区 = %%TEMP%%\KrakenAV_build（ASCII）；部署 = 产品目录
rem  用法: build.bat [部署目录]  （默认本目录的上一级）
rem ============================================================
setlocal
set GCC=D:\AZML\mingw64\bin\g++.exe
set WINDRES=D:\AZML\mingw64\bin\windres.exe
set SRCDIR=%~dp0
set WORK=%TEMP%\KrakenAV_build
set DEPLOY=%~dp0..

rem -- 1) 同步源码到 ASCII 工作区 --
if not exist "%WORK%" mkdir "%WORK%"
robocopy "%SRCDIR%src" "%WORK%\src" /MIR /NFL /NDL /NJH /NJS >nul
robocopy "%SRCDIR%sdk" "%WORK%\sdk" /MIR /NFL /NDL /NJH /NJS >nul
if not exist "%WORK%\src\krakenav.ico" python "%SRCDIR%src\make_icon.py" "%WORK%\src"

rem -- 2) 编译 --
cd /d "%WORK%"
if not exist src\app.res %WINDRES% src\app.rc -O coff -o src\app.res || goto :fail
%GCC% -std=c++17 -municode -mwindows -O2 -static -Wall ^
  -Isdk\include -Isdk\stubinc ^
  src\common.cpp src\engine.cpp src\updater.cpp src\settingsui.cpp src\draw.cpp src\main.cpp ^
  src\app.res sdk\clamav.lib ^
  -lgdiplus -lshlwapi -lole32 -lshell32 -luuid -lgdi32 -luser32 -lcomctl32 -ladvapi32 ^
  -o KrakenAV.exe || goto :fail
%GCC% -std=c++17 -municode -O2 -static -Isdk\include -Isdk\stubinc ^
  src\common.cpp src\engine.cpp src\test_console.cpp sdk\clamav.lib ^
  -lshlwapi -lole32 -lshell32 -luuid -ladvapi32 -o test_console.exe
echo [OK] built KrakenAV.exe + test_console.exe

rem -- 3) 部署 --
if not "%~1"=="" set DEPLOY=%~1
copy /y KrakenAV.exe "%DEPLOY%\KrakenAV.exe" >nul || goto :fail
copy /y test_console.exe "%DEPLOY%\test_console.exe" >nul
echo [OK] deployed to %DEPLOY%
exit /b 0

:fail
echo [FAIL] build error
exit /b 1
