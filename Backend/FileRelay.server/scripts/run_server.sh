#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
IMAGE_NAME=${IMAGE_NAME:-file-relay}
SERVER_VOLUME_HOST=${SERVER_VOLUME_HOST:-${PROJECT_ROOT}/data/server}
SHARED_VOLUME_HOST=${SHARED_VOLUME_HOST:-${PROJECT_ROOT}/data/shared}

mkdir -p "${SERVER_VOLUME_HOST}" "${SHARED_VOLUME_HOST}"

echo "Building ${IMAGE_NAME} image..."
docker build -t "${IMAGE_NAME}" "${PROJECT_ROOT}"

echo "Starting server container..."
docker run --rm \
  --name file-relay-server \
  -e ROLE=server \
  -e SERVER_ARGS="${SERVER_ARGS:-}" \
  -p "${SERVER_PORT:-8080}:${SERVER_PORT:-8080}" \
  -v "${SERVER_VOLUME_HOST}:/opt/file_relay/server_data" \
  -v "${SHARED_VOLUME_HOST}:/opt/file_relay/shared" \
  "${IMAGE_NAME}"
