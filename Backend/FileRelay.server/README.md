# File Relay Transfer Server

This directory (`Backend/FileRelay.server/`) contains the native C++ implementation of the Super
Voice offline transfer pipeline.  It bundles both the ingestion **server** that accepts chunked
uploads and the companion **client** that watches a local directory, compresses new payloads, and
streams the chunks to the server over a multi-channel TCP protocol.

The README describes how to build the binaries, run them directly or inside Docker, configure
them through command-line flags and environment variables, understand the expected on-disk
layout, and exercise the end-to-end harness.

## Repository Layout

```
Backend/FileRelay.server/
├── Client/                 # C++ client that watches a directory and streams chunks
├── Server/                 # C++ server that accepts chunks and assembles payloads
├── common/                 # Shared utilities (headers) used by both components
├── docs/                   # ADRs and work breakdown notes for the protocol
├── scripts/                # Helper scripts (Docker runners, e2e validation)
├── tests/                  # Test fixtures referenced by the harness
├── Dockerfile              # Multi-stage image producing client & server executables
└── CMakeLists.txt          # Top-level build driving both Client/ and Server/
```

The server persists runtime data under a configurable root (default `server_data/`) with the
following structure:

```
server_data/
├── patches/                # One subdirectory per file_id that stores received chunks
│   └── <file_id>/
│       ├── patch_<index>.bin  # Binary payload for each chunk index
│       └── ids.list           # Manifest of committed chunk indexes
└── files/                  # Final assembled and decompressed payloads
```

The `patches/<file_id>` directory is removed automatically after a payload is successfully
assembled and published to `files/`.  The server periodically deletes expired entries based on
the active TTL configuration.【F:Server/src/storage.hpp†L40-L132】【F:Server/src/assembler.hpp†L19-L111】

## Building from Source

The project uses CMake (C++20) and depends on Asio (fetched automatically) and libzstd.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target server_app client_app
```

The resulting binaries live at `build/Server/server_app` and `build/Client/client_app`.  You can
pass `-G Ninja` (or another generator) if preferred.  The Docker image follows the same steps using
the provided `Dockerfile` multi-stage build.【F:CMakeLists.txt†L1-L24】【F:Dockerfile†L1-L37】

## Running the Native Server

Launch the server binary and tailor its listeners with command-line flags:

```bash
./build/Server/server_app \
  --address 0.0.0.0 \
  --sys-base 7000 \
  --data-base 7100 \
  --x 4 \
  --ttl 3600 \
  --root /path/to/server_data
```

Key options:

| Flag | Description |
| --- | --- |
| `--address <addr>` | Bind address for all listeners (defaults to `0.0.0.0`). |
| `--sys-base <port>` | Base port for system channels (health, telemetry, control, ack occupy offsets 0–3). |
| `--data-base <port>` | Base port for data channels (one port per data listener starting here). |
| `--x <count>` | Number of concurrent data listener sockets exposed to clients. |
| `--ttl <seconds>` | Retention window applied to assembled files and incomplete payloads. |
| `--root <path>` | Root directory for `patches/` and `files/` persistence. |

The process writes metrics and lifecycle messages to `stdout`/`stderr`, and responds to
`SIGINT`/`SIGTERM` by shutting down gracefully.【F:Server/src/main.cpp†L26-L155】【F:Server/src/listeners.hpp†L21-L116】

## Docker Build & Runtime

Build the image locally:

```bash
docker build -t file-relay Backend/FileRelay.server
```

The runtime stage exposes both executables under `/opt/file_relay/` and uses a simple
shell entrypoint controlled by environment variables:

- `ROLE` – `server` (default) or `client` to select which binary runs.
- `SERVER_ARGS` – appended to the server binary when `ROLE=server`.
- `CLIENT_ARGS` – appended to the client binary when `ROLE=client`.

Example server container that binds host directories for persistence and exposes ports:

```bash
docker run --rm -d \
  --name file-relay-server \
  -p 7000-7003:7000-7003 \
  -p 7100-7103:7100-7103 \
  -v $(pwd)/data/server:/opt/file_relay/server_data \
  -e ROLE=server \
  -e SERVER_ARGS="--root /opt/file_relay/server_data --sys-base 7000 --data-base 7100 --x 4" \
  file-relay
```

Example client container pointing at the same network namespace and watching a host folder:

```bash
docker run --rm \
  --name file-relay-client \
  --network host \
  -v $(pwd)/data/client:/opt/file_relay/client_data \
  -e ROLE=client \
  -e CLIENT_ARGS="--watch-dir /opt/file_relay/client_data/incoming --connections 2 --host-prefix 127.0.0. --base-port 7100 --control-host 127.0.0.1 --control-port 7000" \
  file-relay
```

Both examples match the defaults used by the automated end-to-end harness.【F:Dockerfile†L18-L46】【F:scripts/run_server.sh†L15-L22】【F:scripts/run_client.sh†L12-L21】【F:scripts/e2e.sh†L161-L205】

## CLI & Environment Configuration

The container orchestration relies on CLI arguments passed through `SERVER_ARGS`/`CLIENT_ARGS`.
The most important knobs (including the ones surfaced in the original planning docs) are
summarised below:

| Option | Applies to | Purpose | Default | Notes |
| --- | --- | --- | --- | --- |
| `--host` | Client | Hostname the client should target for all system channels. | `127.0.0.1` (via `--control-host`) | The plan refers to `--host`; the implementation splits this into `--control-host` for system channels and `--host-prefix` for data sockets. |
| `--sys-base <port>` | Server & Client | Base system port (health/telemetry/control/ack offsets 0–3). | `7000` | Client sets `--control-port`, the server listens on `--sys-base`.【F:Server/src/main.cpp†L47-L112】【F:Client/src/main.cpp†L36-L108】 |
| `--data-base <port>` | Server & Client | First data-channel port. | `7100` | Server exposes `--x` listeners starting at this port; client composes endpoints from `--base-port` (plan name `--data-base`).【F:Server/src/main.cpp†L47-L116】【F:Client/src/main.cpp†L29-L104】 |
| `--x <count>` | Server | Number of concurrent data listeners to accept chunk uploads. | `4` | Adjustable at runtime through the control channel command `SCALE_DATA <count>`.【F:Server/src/main.cpp†L27-L115】【F:Server/src/control.hpp†L41-L78】 |
| `--n` | Server | Payload retention TTL in seconds. | `3600` | Implemented as `--ttl`; the value can also be changed live with `SET_TTL <seconds>`.【F:Server/src/main.cpp†L29-L118】【F:Server/src/control.hpp†L78-L111】 |
| `--client-dir <path>` | Client | Directory scanned for new payloads. | `/opt/file_relay/client_data` | Implemented as `--watch-dir` with identical semantics. Clients compress and enqueue files from this tree. 【F:Client/src/main.cpp†L24-L103】 |

Additional client-focused flags include `--connections`, `--chunk-size`, `--compression-level`,
and retry tuning parameters for establishing TCP sockets.  Server-side flags also expose
`--address` to change the bind interface.  All values may be injected through Docker by setting
the relevant `*_ARGS` environment variables.【F:Client/src/main.cpp†L24-L158】【F:Server/src/main.cpp†L47-L122】

## Multi-Channel Protocol Overview

The TCP interface is split across distinct ports:

- **Health** (`sys-base + 0`): responds with `OK\n` for liveness checks.
- **Telemetry** (`sys-base + 1`): returns a single line with aggregate counters.
- **Control** (`sys-base + 2`): accepts plain-text commands terminated by `\n`.
- **Ack** (`sys-base + 3`): returns `ACK\n` for external orchestrators that require it.
- **Data** (`data-base + i`): each listener handles a single upload stream.

Each data connection sends a newline-delimited header followed by the binary payload:

1. `file_id`
2. `original_name`
3. `index`
4. `total_chunks`
5. `ttl_seconds`
6. `payload_size`
7. `header_crc`
8. `payload_crc`
9. raw payload bytes (exactly `payload_size`)

The server validates CRCs, writes the chunk to
`patches/<file_id>/patch_<index>.bin`, updates `ids.list`, and attempts assembly when all
parts arrive.  Successful requests receive `STORED\n`.  Failed reads increment metrics and
close the connection.【F:Server/src/main.cpp†L117-L229】【F:Server/src/storage.hpp†L69-L155】

Control commands include:

- `SCALE_DATA <count>` – adjust the number of data listeners.
- `SET_TTL <seconds>` – update retention for active and future payloads.
- `PING` / `PONG` – liveness check that also triggers a metrics snapshot.
- `STATUS` – report current data listener count and TTL.
- `QUIT` / `EXIT` – close the control session after acknowledging the last command.

## End-to-End Validation Harness

`scripts/e2e.sh` orchestrates a full roundtrip inside Docker:

1. Builds (unless `--no-build`) the `file-relay` image.
2. Creates host directories for client watch data, server state, and shared logs.
3. Starts a server container (`ROLE=server`) mounted at `/opt/file_relay/server_data`.
4. Waits until the server prints `server running`.
5. Starts a client container (`ROLE=client`) pointed at `/opt/file_relay/client_data/<fixture>`.
6. Generates deterministic fixtures (`roundtrip_small|medium|large.bin`) and copies them into the
   client watch directory.
7. Waits for the roundtrip, validates SHA-256 hashes, records throughput metrics, and tears down
   containers.

The script supports `--fixture <name>` to run a single payload and `--no-build` to skip the
image rebuild when iterating locally.  Logs and metrics are written under `data/e2e/` next to the
project root.【F:scripts/e2e.sh†L1-L221】

## Limitations & Operational Notes

- **No authentication or TLS.** All channels operate in clear text and trust the caller.
- **Single host coordination.** Clients must know the server host/port layout ahead of time; no
  discovery service exists.
- **Limited error recovery.** The server logs parse or CRC errors and drops the chunk; clients are
  responsible for retrying according to their queue policy.
- **In-memory manifests.** Chunk manifests are reconstructed in memory at runtime; unexpected
  process termination before flush may require clients to re-upload missing pieces.
- **Zstandard requirement.** Payloads must be zstd-compressed by the client; assembly assumes the
  format and fails otherwise.
- **Filesystem locality.** All persistence occurs on the local filesystem mounted at `--root`; use
  external volumes when containerising to avoid data loss on container deletion.【F:Server/src/main.cpp†L117-L229】【F:Server/src/storage.hpp†L40-L210】【F:Client/src/main.cpp†L68-L158】

