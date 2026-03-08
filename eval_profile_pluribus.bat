@echo off
setlocal

rem Pluribus-style evaluation preset.
rem Edit TARGET/BASELINES if your artifact names differ.

set "TARGET=data\blueprint_pluribus_16d_final.bin"
set "TARGET_IS_FALLBACK=0"
if not exist "%TARGET%" (
  set "TARGET=data\quick_eval_target_v9.bin"
  set "TARGET_IS_FALLBACK=1"
)

set "BASELINE_A=data\baseline_pluribus_ref_a_v9.bin"
set "BASELINE_B=data\baseline_pluribus_ref_b_v9.bin"
set "BASELINES=%BASELINE_A%;%BASELINE_B%"
set "ABSTRACTION=data\abstraction_prod_pluribus_v1.bin"

set "MODE=runtime-pluribus"
set "HANDS=50000"
set "SEEDS=8"
set "SEED_START=20260304"
set "SEARCH_ITERS=128"
set "SEARCH_DEPTH=3"
set "SEARCH_THREADS=24"
set "SEARCH_PICK=sample-final"
set "OFFTREE_MODE=inject"
set "MIN_BB100=0.0"

set "CSV=data\eval_pluribus_profile.csv"
set "JSON=data\eval_pluribus_profile.json"

if not exist "build\main.exe" (
  call build.bat
  if errorlevel 1 exit /b %errorlevel%
)

if not exist "%ABSTRACTION%" (
  echo Building fallback abstraction artifact: %ABSTRACTION%
  build\main.exe abstraction-build --out "%ABSTRACTION%" --seed 20260301 --bp-flop 200 --bp-turn 200 --bp-river 200 --search-flop 500 --search-turn 500 --search-river 500 --mc-samples 32 --samples 50000 --kmeans-iters 64
  if errorlevel 1 (
    echo Failed to build fallback abstraction artifact.
    exit /b %errorlevel%
  )
)

if "%TARGET_IS_FALLBACK%"=="1" (
  echo Preparing fallback target blueprint with matching abstraction: %TARGET%
  build\main.exe train --iters 2 --threads 1 --abstraction "%ABSTRACTION%" --out "%TARGET%" --dump-iters 0 --status-iters 0 >nul
  if errorlevel 1 (
    echo Failed to generate fallback target blueprint.
    exit /b %errorlevel%
  )
)

if not exist "%BASELINE_A%" (
  echo Seeding baseline A from %TARGET% -> %BASELINE_A%
  copy /Y "%TARGET%" "%BASELINE_A%" >nul
)
if not exist "%BASELINE_B%" (
  echo Seeding baseline B from %TARGET% -> %BASELINE_B%
  copy /Y "%TARGET%" "%BASELINE_B%" >nul
)
for %%B in ("%BASELINE_A%" "%BASELINE_B%") do (
  if not exist "%%~B" (
    echo Missing baseline blueprint: %%~B
    echo Update BASELINE_A/BASELINE_B in eval_profile_pluribus.bat.
    exit /b 1
  )
)

call eval.bat ^
  -Target "%TARGET%" ^
  -Baselines "%BASELINES%" ^
  -Abstraction "%ABSTRACTION%" ^
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
  -Csv "%CSV%" ^
  -Json "%JSON%"
exit /b %errorlevel%
