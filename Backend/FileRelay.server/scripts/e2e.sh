#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: ./scripts/e2e.sh [options]

Run the Dockerised end-to-end harness and verify roundtrip integrity for sample fixtures.

Options:
  --fixture NAME   Only process the specified fixture (roundtrip_small.bin | roundtrip_medium.bin | roundtrip_large.bin).
  --no-build       Skip rebuilding the Docker image (use existing local image tag).
  -h, --help       Show this help message.
USAGE
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: required command '$1' not found" >&2
        exit 1
    fi
}

wait_for_log() {
    local container=$1
    local pattern=$2
    local timeout=${3:-60}
    local elapsed=0
    while (( elapsed < timeout )); do
        if docker logs "$container" 2>&1 | grep -q "$pattern"; then
            return 0
        fi
        sleep 1
        ((elapsed++))
    done
    echo "error: timeout waiting for pattern '$pattern' in container '$container' logs" >&2
    return 1
}

make_fixture() {
    local output=$1
    local size=$2
    python3 - "$output" "$size" <<'PY'
import hashlib
import pathlib
import sys
path = pathlib.Path(sys.argv[1])
size = int(sys.argv[2])
pattern = b"SuperVoiceTestPattern\n"
data = (pattern * ((size // len(pattern)) + 1))[:size]
path.write_bytes(data)
print(hashlib.sha256(data).hexdigest(), end="")
PY
}

calculate_throughput() {
    local size_bytes=$1
    local duration_ms=$2
    python3 - "$size_bytes" "$duration_ms" <<'PY'
import sys
size = int(sys.argv[1])
duration = int(sys.argv[2])
if duration == 0:
    print("inf")
else:
    seconds = duration / 1000.0
    mib = size / 1048576.0
    print(f"{mib / seconds:.3f}")
PY
}

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

LEGACY_IMAGE=${LOCAL_AI_IMAGE:-${LOCAL_AI_MODEL_IMAGE:-}}
if [[ -z ${IMAGE_NAME:-} && -n ${LEGACY_IMAGE} ]]; then
    IMAGE_NAME=${LEGACY_IMAGE}
fi
IMAGE_NAME=${IMAGE_NAME:-file-relay}

if [[ -z ${SERVER_CONTAINER:-} ]]; then
    SERVER_CONTAINER=${LOCAL_AI_SERVER_CONTAINER:-file-relay-e2e-server}
fi
if [[ -z ${CLIENT_CONTAINER:-} ]]; then
    CLIENT_CONTAINER=${LOCAL_AI_CLIENT_CONTAINER:-file-relay-e2e-client}
fi
DOCKER_NETWORK=${DOCKER_NETWORK:-host}
if [[ -z ${E2E_ROOT:-} ]]; then
    E2E_ROOT=${LOCAL_AI_E2E_ROOT:-${PROJECT_ROOT}/data/e2e}
fi
CLIENT_VOLUME_HOST=${CLIENT_VOLUME_HOST:-${LOCAL_AI_CLIENT_VOLUME_HOST:-${E2E_ROOT}/client}}
SERVER_VOLUME_HOST=${SERVER_VOLUME_HOST:-${LOCAL_AI_SERVER_VOLUME_HOST:-${E2E_ROOT}/server}}
SHARED_VOLUME_HOST=${SHARED_VOLUME_HOST:-${LOCAL_AI_SHARED_VOLUME_HOST:-${E2E_ROOT}/shared}}
LOG_DIR=${LOG_DIR:-${E2E_ROOT}/logs}
METRICS_FILE=${METRICS_FILE:-${LOG_DIR}/metrics.csv}
CLIENT_WATCH_RELATIVE=${CLIENT_WATCH_RELATIVE:-incoming}
CLIENT_WATCH_DIR=${CLIENT_VOLUME_HOST}/${CLIENT_WATCH_RELATIVE}
SERVER_FILES_DIR=${SERVER_VOLUME_HOST}/files

BUILD_IMAGE=1
REQUESTED_FIXTURE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
    --fixture)
        if [[ $# -lt 2 ]]; then
            echo "error: --fixture requires a value" >&2
            exit 1
        fi
        REQUESTED_FIXTURE=$2
        shift 2
        ;;
    --no-build)
        BUILD_IMAGE=0
        shift
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        echo "error: unknown option '$1'" >&2
        usage
        exit 1
        ;;
    esac
done

require_command docker
require_command python3
require_command sha256sum
require_command date

mkdir -p "$CLIENT_WATCH_DIR" "$SERVER_VOLUME_HOST" "$SHARED_VOLUME_HOST" "$LOG_DIR"
find "$CLIENT_WATCH_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
rm -rf "${SERVER_VOLUME_HOST:?}/patches" "${SERVER_VOLUME_HOST:?}/files"
mkdir -p "$SERVER_FILES_DIR"

declare -A FIXTURE_SIZES=(
    [roundtrip_small.bin]=4096
    [roundtrip_medium.bin]=524288
    [roundtrip_large.bin]=2097152
)

declare -A EXPECTED_HASHES=(
    [roundtrip_small.bin]=37069a6d24c6d8854fb37fcddbc06904f2cfe7a4cc21f72594493b0893d157f9
    [roundtrip_medium.bin]=e112aebd828bef408fce72b834efe1f8c0d79c0df30eb2668655191f8eefd6f2
    [roundtrip_large.bin]=0b1ca26666e8dd13274c4d328d3e46b6403fb9c56c751091c5ce98ccff60bc04
)

FIXTURE_ORDER=(roundtrip_small.bin roundtrip_medium.bin roundtrip_large.bin)

if [[ -n "$REQUESTED_FIXTURE" ]]; then
    if [[ -z ${FIXTURE_SIZES[$REQUESTED_FIXTURE]:-} ]]; then
        echo "error: unknown fixture '$REQUESTED_FIXTURE'" >&2
        exit 1
    fi
    FIXTURE_ORDER=($REQUESTED_FIXTURE)
fi

if [[ ${BUILD_IMAGE} -eq 1 ]]; then
    echo "[e2e] Building Docker image '${IMAGE_NAME}'..."
    docker build -t "$IMAGE_NAME" "$PROJECT_ROOT"
else
    echo "[e2e] Skipping Docker build (using existing image '${IMAGE_NAME}')."
fi

cleanup() {
    local exit_code=$1
    mkdir -p "$LOG_DIR"
    if docker ps -a --format '{{.Names}}' | grep -Fxq "$CLIENT_CONTAINER"; then
        docker logs "$CLIENT_CONTAINER" >"${LOG_DIR}/client.log" 2>&1 || true
        docker rm -f "$CLIENT_CONTAINER" >/dev/null 2>&1 || true
    fi
    if docker ps -a --format '{{.Names}}' | grep -Fxq "$SERVER_CONTAINER"; then
        docker logs "$SERVER_CONTAINER" >"${LOG_DIR}/server.log" 2>&1 || true
        docker rm -f "$SERVER_CONTAINER" >/dev/null 2>&1 || true
    fi
    exit "$exit_code"
}

trap 'cleanup "$?"' EXIT

if [[ ! -f "$METRICS_FILE" ]]; then
    echo "fixture,size_bytes,start_ms,end_ms,duration_ms,throughput_mib_s" >"$METRICS_FILE"
fi

SERVER_ENV_ARGS=(
    -e ROLE=server
    -e SERVER_ARGS="--root /opt/file_relay/server_data --sys-base 7000 --data-base 7100"
)

CLIENT_ENV_ARGS=(
    -e ROLE=client
    -e CLIENT_ARGS="--watch-dir /opt/file_relay/client_data/${CLIENT_WATCH_RELATIVE} --scan-interval-ms 500 --connections 1 --host-prefix 127.0.0. --base-port 7100 --control-host 127.0.0.1 --control-port 7000"
)

echo "[e2e] Starting server container '${SERVER_CONTAINER}'..."
docker run -d --rm \
    --name "$SERVER_CONTAINER" \
    --network "$DOCKER_NETWORK" \
    -v "${SERVER_VOLUME_HOST}:/opt/file_relay/server_data" \
    -v "${SHARED_VOLUME_HOST}:/opt/file_relay/shared" \
    "${SERVER_ENV_ARGS[@]}" \
    "$IMAGE_NAME"

wait_for_log "$SERVER_CONTAINER" "server running" 90

echo "[e2e] Starting client container '${CLIENT_CONTAINER}'..."
docker run -d --rm \
    --name "$CLIENT_CONTAINER" \
    --network "$DOCKER_NETWORK" \
    -v "${CLIENT_VOLUME_HOST}:/opt/file_relay/client_data" \
    -v "${SHARED_VOLUME_HOST}:/opt/file_relay/shared" \
    "${CLIENT_ENV_ARGS[@]}" \
    "$IMAGE_NAME"

sleep 3

run_fixture() {
    local fixture=$1
    local size=${FIXTURE_SIZES[$fixture]}
    local expected=${EXPECTED_HASHES[$fixture]}
    local client_path="${CLIENT_WATCH_DIR}/${fixture}"
    local server_path="${SERVER_FILES_DIR}/${fixture}"

    rm -f "$client_path" "$server_path" "${server_path}.part"

    local start_ms
    start_ms=$(date +%s%3N)
    local actual_hash
    actual_hash=$(make_fixture "$client_path" "$size")
    if [[ "$actual_hash" != "$expected" ]]; then
        echo "error: generated hash for $fixture ($actual_hash) does not match expected $expected" >&2
        return 1
    fi

    echo "[e2e] Waiting for server artifact '$fixture'..."
    local timeout=300
    local waited=0
    while (( waited < timeout )); do
        if [[ -f "$server_path" ]]; then
            break
        fi
        sleep 1
        ((waited++))
    done

    if [[ ! -f "$server_path" ]]; then
        echo "error: timed out waiting for server artifact '$fixture'" >&2
        return 1
    fi

    local server_hash
    server_hash=$(sha256sum "$server_path" | awk '{print $1}')
    if [[ "$server_hash" != "$expected" ]]; then
        echo "error: checksum mismatch for $fixture (expected $expected, got $server_hash)" >&2
        return 1
    fi

    local end_ms
    end_ms=$(date +%s%3N)
    local duration_ms=$((end_ms - start_ms))
    local throughput
    throughput=$(calculate_throughput "$size" "$duration_ms")

    echo "[e2e] Fixture $fixture completed in ${duration_ms} ms (throughput ${throughput} MiB/s)."
    echo "$fixture,$size,$start_ms,$end_ms,$duration_ms,$throughput" >>"$METRICS_FILE"
}

for fixture in "${FIXTURE_ORDER[@]}"; do
    run_fixture "$fixture"
    rm -f "${CLIENT_WATCH_DIR}/${fixture}"
    rm -f "${SERVER_FILES_DIR}/${fixture}.part"
    rm -rf "${SERVER_VOLUME_HOST}/patches"/${fixture%.bin} 2>/dev/null || true
    rm -rf "${SERVER_VOLUME_HOST}/patches"/${fixture} 2>/dev/null || true
    rm -rf "${SERVER_VOLUME_HOST}/patches" 2>/dev/null || true
    mkdir -p "$SERVER_FILES_DIR"
done

echo "[e2e] Stopping client container..."
docker stop "$CLIENT_CONTAINER" >/dev/null 2>&1 || true

echo "[e2e] Stopping server container..."
docker stop "$SERVER_CONTAINER" >/dev/null 2>&1 || true

trap - EXIT
cleanup 0
