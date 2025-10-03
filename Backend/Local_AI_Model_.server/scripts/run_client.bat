@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "PROJECT_ROOT=%%~fI"

if not defined IMAGE_NAME set "IMAGE_NAME=local-ai-model"
if not defined CLIENT_VOLUME_HOST set "CLIENT_VOLUME_HOST=%PROJECT_ROOT%\data\client"
if not defined SHARED_VOLUME_HOST set "SHARED_VOLUME_HOST=%PROJECT_ROOT%\data\shared"

if not exist "%CLIENT_VOLUME_HOST%" mkdir "%CLIENT_VOLUME_HOST%"
if not exist "%SHARED_VOLUME_HOST%" mkdir "%SHARED_VOLUME_HOST%"

echo Building %IMAGE_NAME% image...
docker build -t %IMAGE_NAME% "%PROJECT_ROOT%"

echo Starting client container...
docker run --rm ^
  --name local-ai-client ^
  -e ROLE=client ^
  -e CLIENT_ARGS="%CLIENT_ARGS%" ^
  -v "%CLIENT_VOLUME_HOST%:/opt/local_ai_model/client_data" ^
  -v "%SHARED_VOLUME_HOST%:/opt/local_ai_model/shared" ^
  %IMAGE_NAME%
