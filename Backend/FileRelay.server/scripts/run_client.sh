#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
IMAGE_NAME=${IMAGE_NAME:-file-relay}
CLIENT_VOLUME_HOST=${CLIENT_VOLUME_HOST:-${PROJECT_ROOT}/data/client}
SHARED_VOLUME_HOST=${SHARED_VOLUME_HOST:-${PROJECT_ROOT}/data/shared}

mkdir -p "${CLIENT_VOLUME_HOST}" "${SHARED_VOLUME_HOST}"

echo "Building ${IMAGE_NAME} image..."
docker build -t "${IMAGE_NAME}" "${PROJECT_ROOT}"

echo "Starting client container..."
docker run --rm \
  --name file-relay-client \
  -e ROLE=client \
  -e CLIENT_ARGS="${CLIENT_ARGS:-}" \
  -v "${CLIENT_VOLUME_HOST}:/opt/file_relay/client_data" \
  -v "${SHARED_VOLUME_HOST}:/opt/file_relay/shared" \
  "${IMAGE_NAME}"
