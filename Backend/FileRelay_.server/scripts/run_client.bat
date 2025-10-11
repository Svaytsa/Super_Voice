@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "PROJECT_ROOT=%%~fI"

if not defined IMAGE_NAME (
  if defined LOCAL_AI_IMAGE (
    set "IMAGE_NAME=%LOCAL_AI_IMAGE%"
  ) else (
    if defined LOCAL_AI_MODEL_IMAGE (
      set "IMAGE_NAME=%LOCAL_AI_MODEL_IMAGE%"
    ) else (
      set "IMAGE_NAME=file-relay"
    )
  )
)
if not defined CLIENT_VOLUME_HOST (
  if defined LOCAL_AI_CLIENT_VOLUME_HOST (
    set "CLIENT_VOLUME_HOST=%LOCAL_AI_CLIENT_VOLUME_HOST%"
  ) else (
    set "CLIENT_VOLUME_HOST=%PROJECT_ROOT%\data\client"
  )
)
if not defined SHARED_VOLUME_HOST (
  if defined LOCAL_AI_SHARED_VOLUME_HOST (
    set "SHARED_VOLUME_HOST=%LOCAL_AI_SHARED_VOLUME_HOST%"
  ) else (
    set "SHARED_VOLUME_HOST=%PROJECT_ROOT%\data\shared"
  )
)
if not defined CLIENT_CONTAINER (
  if defined LOCAL_AI_CLIENT_CONTAINER (
    set "CLIENT_CONTAINER=%LOCAL_AI_CLIENT_CONTAINER%"
  ) else (
    set "CLIENT_CONTAINER=file-relay-client"
  )
)

if not exist "%CLIENT_VOLUME_HOST%" mkdir "%CLIENT_VOLUME_HOST%"
if not exist "%SHARED_VOLUME_HOST%" mkdir "%SHARED_VOLUME_HOST%"

echo Building %IMAGE_NAME% image...
docker build -t %IMAGE_NAME% "%PROJECT_ROOT%"

echo Starting client container...
docker run --rm ^
  --name %CLIENT_CONTAINER% ^
  -e ROLE=client ^
  -e CLIENT_ARGS="%CLIENT_ARGS%" ^
  -v "%CLIENT_VOLUME_HOST%:/opt/file_relay/client_data" ^
  -v "%SHARED_VOLUME_HOST%:/opt/file_relay/shared" ^
  %IMAGE_NAME%
