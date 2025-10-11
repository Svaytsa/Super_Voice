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
if not defined SERVER_VOLUME_HOST (
  if defined LOCAL_AI_SERVER_VOLUME_HOST (
    set "SERVER_VOLUME_HOST=%LOCAL_AI_SERVER_VOLUME_HOST%"
  ) else (
    set "SERVER_VOLUME_HOST=%PROJECT_ROOT%\data\server"
  )
)
if not defined SHARED_VOLUME_HOST (
  if defined LOCAL_AI_SHARED_VOLUME_HOST (
    set "SHARED_VOLUME_HOST=%LOCAL_AI_SHARED_VOLUME_HOST%"
  ) else (
    set "SHARED_VOLUME_HOST=%PROJECT_ROOT%\data\shared"
  )
)
if not defined SERVER_PORT (
  if defined LOCAL_AI_SERVER_PORT (
    set "SERVER_PORT=%LOCAL_AI_SERVER_PORT%"
  ) else (
    set "SERVER_PORT=8080"
  )
)
if not defined SERVER_CONTAINER (
  if defined LOCAL_AI_SERVER_CONTAINER (
    set "SERVER_CONTAINER=%LOCAL_AI_SERVER_CONTAINER%"
  ) else (
    set "SERVER_CONTAINER=file-relay-server"
  )
)

if not exist "%SERVER_VOLUME_HOST%" mkdir "%SERVER_VOLUME_HOST%"
if not exist "%SHARED_VOLUME_HOST%" mkdir "%SHARED_VOLUME_HOST%"

echo Building %IMAGE_NAME% image...
docker build -t %IMAGE_NAME% "%PROJECT_ROOT%"

echo Starting server container...
docker run --rm ^
  --name %SERVER_CONTAINER% ^
  -e ROLE=server ^
  -e SERVER_ARGS="%SERVER_ARGS%" ^
  -p %SERVER_PORT%:%SERVER_PORT% ^
  -v "%SERVER_VOLUME_HOST%:/opt/file_relay/server_data" ^
  -v "%SHARED_VOLUME_HOST%:/opt/file_relay/shared" ^
  %IMAGE_NAME%
