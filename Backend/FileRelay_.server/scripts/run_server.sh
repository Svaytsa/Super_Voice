#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
LEGACY_IMAGE=${LOCAL_AI_IMAGE:-${LOCAL_AI_MODEL_IMAGE:-}}
if [[ -z ${IMAGE_NAME:-} && -n ${LEGACY_IMAGE} ]]; then
    IMAGE_NAME=${LEGACY_IMAGE}
fi
IMAGE_NAME=${IMAGE_NAME:-file-relay}
SERVER_VOLUME_HOST=${SERVER_VOLUME_HOST:-${LOCAL_AI_SERVER_VOLUME_HOST:-${PROJECT_ROOT}/data/server}}
SHARED_VOLUME_HOST=${SHARED_VOLUME_HOST:-${LOCAL_AI_SHARED_VOLUME_HOST:-${PROJECT_ROOT}/data/shared}}
SERVER_CONTAINER=${SERVER_CONTAINER:-${LOCAL_AI_SERVER_CONTAINER:-file-relay-server}}
SERVER_PORT=${SERVER_PORT:-${LOCAL_AI_SERVER_PORT:-8080}}

mkdir -p "${SERVER_VOLUME_HOST}" "${SHARED_VOLUME_HOST}"

echo "Building ${IMAGE_NAME} image..."
docker build -t "${IMAGE_NAME}" "${PROJECT_ROOT}"

echo "Starting server container..."
docker run --rm \
  --name "${SERVER_CONTAINER}" \
  -e ROLE=server \
  -e SERVER_ARGS="${SERVER_ARGS:-}" \
  -p "${SERVER_PORT}:${SERVER_PORT}" \
  -v "${SERVER_VOLUME_HOST}:/opt/file_relay/server_data" \
  -v "${SHARED_VOLUME_HOST}:/opt/file_relay/shared" \
  "${IMAGE_NAME}"
