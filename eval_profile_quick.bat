@echo off
setlocal

rem Quick smoke evaluation preset (fast pipeline check, not a strength gate).
rem By default it evaluates target vs itself so it works as a sanity run.
rem For real model comparison, set BASELINES to strong reference blueprints.

set "TARGET="
if exist "data\blueprint_pluribus_16d_final.bin" set "TARGET=data\blueprint_pluribus_16d_final.bin"
if not defined TARGET if exist "data\blueprint.bin" set "TARGET=data\blueprint.bin"
if not defined TARGET set "TARGET=data\quick_eval_target_v9.bin"

if not exist "%TARGET%" (
  if not exist "build\main.exe" (
    call build.bat
    if errorlevel 1 exit /b %errorlevel%
  )
  echo Generating quick smoke target blueprint: %TARGET%
  build\main.exe train --iters 2 --threads 1 --out "%TARGET%" --dump-iters 0 --status-iters 0 >nul
  if errorlevel 1 (
    echo Failed to generate quick smoke target blueprint.
    exit /b %errorlevel%
  )
)

set "BASELINES=%TARGET%"
set "MODE=blueprint"
set "HANDS=2000"
set "SEEDS=2"
set "SEED_START=20260304"
set "SEARCH_ITERS=64"
set "SEARCH_DEPTH=2"
set "SEARCH_THREADS=8"
set "SEARCH_PICK=sample-final"
set "OFFTREE_MODE=inject"
set "MIN_BB100=-100000"
set "CSV=data\eval_quick_profile.csv"
set "JSON=data\eval_quick_profile.json"

echo Quick eval target: %TARGET%
echo Quick eval baselines: %BASELINES%

call eval.bat ^
  -Target "%TARGET%" ^
  -Baselines "%BASELINES%" ^
  -Mode %MODE% ^
  -Hands %HANDS% ^
  -Seeds %SEEDS% ^
  -SeedStart %SEED_START% ^
  -SearchIters %SEARCH_ITERS% ^
  -SearchDepth %SEARCH_DEPTH% ^
  -SearchThreads %SEARCH_THREADS% ^
  -SearchPick %SEARCH_PICK% ^
  -OfftreeMode %OFFTREE_MODE% ^
  -MinBb100 %MIN_BB100% ^
  -IgnoreAbstractionCompat ^
  -Csv "%CSV%" ^
  -Json "%JSON%"
exit /b %errorlevel%
