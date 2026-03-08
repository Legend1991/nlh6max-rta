@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Usage:
rem   run_pluribus_16d_32c.bat [total_seconds] [threads] [min_threads]
rem Defaults:
rem   total_seconds = 1860000
rem   threads       = 12
rem   min_threads   = 12
rem   checkpoint_seconds = 1800

set "TOTAL_SECONDS=1860000"
set "THREADS=12"
set "MIN_THREADS=12"
set "SLICE_SECONDS=21600"
set "CHECKPOINT_SECONDS=900"

if not "%~1"=="" set "TOTAL_SECONDS=%~1"
if not "%~2"=="" set "THREADS=%~2"
if not "%~3"=="" set "MIN_THREADS=%~3"

set "OUT=data\blueprint_pluribus_16d.bin"
set "RUNTIME_OUT=data\blueprint_pluribus_16d_runtime.bin"
set "ABS=data\abstraction_prod_pluribus_v1.bin"
set "SNAP_DIR=data\snapshots"
set "STATE_FILE=data\run_pluribus_16d_32c.state"
set "LOG_FILE=data\run_pluribus_16d_32c.log"

if not exist "build\main.exe" (
  call build.bat
  if errorlevel 1 exit /b %errorlevel%
)

if not exist "data" mkdir data
if not exist "%SNAP_DIR%" mkdir "%SNAP_DIR%"

if not exist "%ABS%" (
  echo Building frozen production abstraction artifact: %ABS%
  echo NOTE: this one-time abstraction build can take a long time with little output.
  echo       If build\main.exe CPU time keeps increasing, it is still working.
  build\main.exe abstraction-build --out "%ABS%" --seed 20260301 --bp-flop 200 --bp-turn 200 --bp-river 200 --search-flop 500 --search-turn 500 --search-river 500 --mc-samples 32 --samples 50000 --kmeans-iters 64
  if errorlevel 1 exit /b %errorlevel%
)

set "ACC_SECONDS=0"
if exist "%STATE_FILE%" (
  for /f "usebackq tokens=1,* delims==" %%A in ("%STATE_FILE%") do (
    if /I "%%A"=="ACC_SECONDS" set "ACC_SECONDS=%%B"
  )
)
if "!ACC_SECONDS!"=="" set "ACC_SECONDS=0"

rem Recover elapsed seconds from existing blueprint header when state file is missing.
if "!ACC_SECONDS!"=="0" if exist "%OUT%" (
  for /f %%I in ('powershell -NoProfile -Command "$fs=[System.IO.File]::OpenRead('%OUT%'); $br=New-Object System.IO.BinaryReader($fs); $null=$br.ReadUInt64(); $null=$br.ReadUInt32(); $null=$br.ReadUInt32(); $null=$br.ReadUInt64(); $null=$br.ReadUInt64(); $null=$br.ReadUInt64(); $null=$br.ReadUInt32(); $null=$br.ReadUInt32(); $null=$br.ReadUInt32(); $null=$br.ReadUInt32(); $elapsed=$br.ReadUInt64(); $br.Close(); $fs.Close(); Write-Output $elapsed"') do set "ACC_SECONDS=%%I"
)

echo ==================================================>> "%LOG_FILE%"
echo Starting/resuming long run at %DATE% %TIME%>> "%LOG_FILE%"
echo total_seconds=!TOTAL_SECONDS! threads=!THREADS! min_threads=!MIN_THREADS! acc_seconds=!ACC_SECONDS!>> "%LOG_FILE%"
echo output=!OUT! abstraction=!ABS! checkpoint_seconds=!CHECKPOINT_SECONDS!>> "%LOG_FILE%"
echo ==================================================>> "%LOG_FILE%"

:loop
if !ACC_SECONDS! GEQ %TOTAL_SECONDS% goto done

set /a REMAIN_SECONDS=%TOTAL_SECONDS%-!ACC_SECONDS!
set /a RUN_SECONDS=%SLICE_SECONDS%
if !REMAIN_SECONDS! LSS !RUN_SECONDS! set /a RUN_SECONDS=!REMAIN_SECONDS!

set "RESUME_ARGS="
if exist "%OUT%" (
  set "RESUME_ARGS=--resume %OUT%"
)

for /f %%I in ('powershell -NoProfile -Command "[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()"') do set "SLICE_START=%%I"

echo [%DATE% %TIME%] acc=!ACC_SECONDS! remain=!REMAIN_SECONDS! run=!RUN_SECONDS! threads=!THREADS! min_threads=!MIN_THREADS!>> "%LOG_FILE%"
echo [%DATE% %TIME%] acc=!ACC_SECONDS! remain=!REMAIN_SECONDS! run=!RUN_SECONDS! threads=!THREADS! min_threads=!MIN_THREADS!

call build\main.exe train --pluribus-profile --threads !THREADS! --min-threads !MIN_THREADS! --parallel-mode sharded --chunk-iters 10000 --samples-per-player 1 --seconds !RUN_SECONDS! --dump-iters 0 --dump-seconds !CHECKPOINT_SECONDS! --status-iters 20000 --snapshot-iters 0 --snapshot-seconds 12000 --snapshot-start-seconds 48000 --avg-start-seconds 48000 --warmup-seconds 48000 --discount-every-seconds 600 --snapshot-dir "%SNAP_DIR%" --abstraction "%ABS%" --out "%OUT%" --int-regret --regret-floor -310000000 --prune-threshold -300000000 --strict-time-phases --discount-stop-seconds 24000 --prune-start-seconds 12000 --no-async-checkpoint !RESUME_ARGS!
if errorlevel 1 (
  echo [%DATE% %TIME%] ERROR: train command failed>> "%LOG_FILE%"
  exit /b 1
)

for /f %%I in ('powershell -NoProfile -Command "[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()"') do set "SLICE_END=%%I"
set /a SLICE_ACTUAL=!SLICE_END!-!SLICE_START!
if !SLICE_ACTUAL! LEQ 0 set /a SLICE_ACTUAL=!RUN_SECONDS!
set /a ACC_SECONDS=!ACC_SECONDS!+!SLICE_ACTUAL!

(
  echo ACC_SECONDS=!ACC_SECONDS!
  echo LAST_SLICE_SECONDS=!SLICE_ACTUAL!
  echo TOTAL_SECONDS=%TOTAL_SECONDS%
  echo THREADS=%THREADS%
  echo MIN_THREADS=%MIN_THREADS%
) > "%STATE_FILE%"

goto loop

:done
set /a CORE_HOURS=!ACC_SECONDS!*%THREADS%/3600
echo [%DATE% %TIME%] Finalizing runtime blueprint into %RUNTIME_OUT%>> "%LOG_FILE%"
call build\main.exe finalize-blueprint --raw "%OUT%" --snapshot-dir "%SNAP_DIR%" --snapshot-min-seconds 48000 --abstraction "%ABS%" --runtime-out "%RUNTIME_OUT%"
if errorlevel 1 (
  echo [%DATE% %TIME%] ERROR: finalize-blueprint failed>> "%LOG_FILE%"
  exit /b 1
)
echo [%DATE% %TIME%] DONE acc_seconds=!ACC_SECONDS! core_hours=!CORE_HOURS!>> "%LOG_FILE%"
echo DONE acc_seconds=!ACC_SECONDS! core_hours=!CORE_HOURS!
echo State file: %STATE_FILE%
echo Log file: %LOG_FILE%
echo Runtime blueprint: %RUNTIME_OUT%
exit /b 0
