@echo off
setlocal EnableExtensions

set "ABS=data\abstraction_prod_pluribus_v1.bin"
set "ABS_REPRO=data\abstraction_prod_pluribus_v1_repro.bin"

if not exist "build\main.exe" (
  call build.bat
  if errorlevel 1 exit /b %errorlevel%
)

if not exist "data" mkdir data

echo Building production abstraction: %ABS%
build\main.exe abstraction-build --out "%ABS%" --seed 20260301 --bp-flop 200 --bp-turn 200 --bp-river 200 --search-flop 500 --search-turn 500 --search-river 500 --mc-samples 32 --samples 50000 --kmeans-iters 64
if errorlevel 1 exit /b %errorlevel%

echo Building reproducibility twin: %ABS_REPRO%
build\main.exe abstraction-build --out "%ABS_REPRO%" --seed 20260301 --bp-flop 200 --bp-turn 200 --bp-river 200 --search-flop 500 --search-turn 500 --search-river 500 --mc-samples 32 --samples 50000 --kmeans-iters 64
if errorlevel 1 exit /b %errorlevel%

fc /b "%ABS%" "%ABS_REPRO%" >nul
if errorlevel 1 (
  echo ERROR: abstraction reproducibility check failed.
  exit /b 1
)

echo OK: frozen abstraction artifact built and reproduced.
echo File: %ABS%
exit /b 0
