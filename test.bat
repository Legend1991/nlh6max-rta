@echo off
setlocal

set "VCVARS="
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS (
  echo Could not find vcvars64.bat for Visual Studio 2022.
  exit /b 1
)

call "%VCVARS%"
if errorlevel 1 exit /b %errorlevel%

if not exist "build" mkdir build

cl /nologo /W4 /wd4505 /TC /Ithird_party\unity\src tests\test_main.c third_party\unity\src\unity.c /Fo:build\ /Fe:build\test_main.exe
if errorlevel 1 exit /b %errorlevel%

build\test_main.exe
exit /b %errorlevel%
