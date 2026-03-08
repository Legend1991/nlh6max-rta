@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Full pipeline:
rem  1) 16-day train/finalize
rem  2) post-run evaluation gates
rem Usage:
rem   run_pluribus_16d_32c_pipeline.bat [total_seconds] [threads]

set "TOTAL_SECONDS=1860000"
set "THREADS=24"
if not "%~1"=="" set "TOTAL_SECONDS=%~1"
if not "%~2"=="" set "THREADS=%~2"

set "PIPE_LOG=data\run_pluribus_16d_pipeline.log"
if not exist "data" mkdir data

echo ==================================================>> "%PIPE_LOG%"
echo Pipeline start: %DATE% %TIME% total_seconds=%TOTAL_SECONDS% threads=%THREADS%>> "%PIPE_LOG%"
echo ==================================================>> "%PIPE_LOG%"

call run_pluribus_16d_32c.bat %TOTAL_SECONDS% %THREADS%
if errorlevel 1 (
  echo [%DATE% %TIME%] ERROR: run_pluribus_16d_32c.bat failed>> "%PIPE_LOG%"
  exit /b 1
)

echo [%DATE% %TIME%] 16-day run completed. Starting evaluation...>> "%PIPE_LOG%"
call eval_profile_pluribus.bat
set "EVAL_RC=%ERRORLEVEL%"
echo [%DATE% %TIME%] eval_profile_pluribus.bat exit=%EVAL_RC%>> "%PIPE_LOG%"

if not "%EVAL_RC%"=="0" (
  echo [%DATE% %TIME%] PIPELINE RESULT: FAIL (evaluation gates failed)>> "%PIPE_LOG%"
  exit /b %EVAL_RC%
)

echo [%DATE% %TIME%] PIPELINE RESULT: PASS>> "%PIPE_LOG%"
exit /b 0
