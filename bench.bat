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

cl /nologo /W4 /O2 /TC tests\bench_eval.c /Fo:build\bench_eval.obj /Fe:build\bench_eval.exe
if errorlevel 1 exit /b %errorlevel%

if "%~1"=="" (
  build\bench_eval.exe
) else (
  build\bench_eval.exe %~1
)
exit /b %errorlevel%
