#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
LEGACY_IMAGE=${LOCAL_AI_IMAGE:-${LOCAL_AI_MODEL_IMAGE:-}}
if [[ -z ${IMAGE_NAME:-} && -n ${LEGACY_IMAGE} ]]; then
    IMAGE_NAME=${LEGACY_IMAGE}
fi
IMAGE_NAME=${IMAGE_NAME:-file-relay}
CLIENT_VOLUME_HOST=${CLIENT_VOLUME_HOST:-${LOCAL_AI_CLIENT_VOLUME_HOST:-${PROJECT_ROOT}/data/client}}
SHARED_VOLUME_HOST=${SHARED_VOLUME_HOST:-${LOCAL_AI_SHARED_VOLUME_HOST:-${PROJECT_ROOT}/data/shared}}
CLIENT_CONTAINER=${CLIENT_CONTAINER:-${LOCAL_AI_CLIENT_CONTAINER:-file-relay-client}}

mkdir -p "${CLIENT_VOLUME_HOST}" "${SHARED_VOLUME_HOST}"

echo "Building ${IMAGE_NAME} image..."
docker build -t "${IMAGE_NAME}" "${PROJECT_ROOT}"

echo "Starting client container..."
docker run --rm \
  --name "${CLIENT_CONTAINER}" \
  -e ROLE=client \
  -e CLIENT_ARGS="${CLIENT_ARGS:-}" \
  -v "${CLIENT_VOLUME_HOST}:/opt/file_relay/client_data" \
  -v "${SHARED_VOLUME_HOST}:/opt/file_relay/shared" \
  "${IMAGE_NAME}"
