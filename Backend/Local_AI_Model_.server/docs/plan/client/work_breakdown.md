# Client Work Breakdown

## Scope
Decompose the client into modular components that watch a directory for new data, compress
payloads, split them into fixed-size chunks, queue work for transmission, and send the
chunks over persistent socket connections while publishing system telemetry.

## Modules

### `watcher.hpp`
- **Responsibility:** Periodically scan the configured client directory for new or modified
  files and enqueue discovered payload descriptors for further processing.
- **Operation:**
  - Poll `--client-dir` every configurable period (default 1 s) using `std::filesystem`.
  - Maintain a cache of file modification timestamps / sizes to detect changes.
  - For each new payload, emit metadata (path, size, last write time) into the chunking
    stage.
- **Concurrency:** Runs in its own thread, sleeping for the scan period between iterations.
- **Configuration:** Accepts the scan period via constructor argument (defaults to 1000 ms).

### `compressor.hpp`
- **Responsibility:** Apply Zstandard compression with the library's default compression level.
- **Operation:**
  - Expose `compress(std::span<const std::byte>) -> std::vector<std::byte>`.
  - Use streaming interface for large files; fall back to single-shot helper for small
    buffers.
  - Record compression ratio for telemetry (fed into system channels).
- **Dependencies:** Wraps the shared Zstd helper utilities located under `common/` if available.

### `chunker.hpp`
- **Responsibility:** Split compressed payloads into chunks of approximately 2_500_000 bytes.
- **Operation:**
  - Accepts metadata + compressed buffer.
  - Emits a sequence of chunk structs containing payload id, chunk index, total chunks, and
    `std::span` of bytes (copy-once into queue).
  - The last chunk may be smaller than 2_500_000 bytes.
- **Configuration:** Chunk size constant defined in this header; expose setter for testing.

### `queue.hpp`
- **Responsibility:** Provide a bounded multi-producer/multi-consumer queue for chunks.
- **Characteristics:**
  - Capacity fixed at 2048 entries.
  - Implemented with `std::mutex` + `std::condition_variable` to block producers when full
    and consumers when empty.
  - Support graceful shutdown via `close()` that wakes waiters.
  - Template queue so it can be reused for system messages if needed.

### `sender.hpp`
- **Responsibility:** Push chunk messages over `X` persistent data sockets using a round-robin
  scheduling strategy with retry logic.
- **Operation:**
  - Maintain `X` (from `--x`) connected TCP sockets to `--host` on ports derived from
    `--data-base`.
  - For each dequeued chunk, select the next socket in round-robin order.
  - Attempt to send the chunk with up to 3 retries on transient failures; on permanent
    errors, recycle the socket (close + reconnect).
  - Track throughput metrics for system channels.
- **Concurrency:** Dedicated thread per socket for reconnect handling plus a dispatcher thread
  reading from the queue.

### `system_channels.hpp`
- **Responsibility:** Manage four persistent telemetry/control channels.
- **Operation:**
  - Establish sockets to `--host` using base port `--sys-base` and offsets `0..3`.
  - Publish periodic `QUEUE_SIZE_UPDATE` messages (current queue depth, compression ratio,
    throughput) approximately every 500 ms.
  - Provide helpers to send ad-hoc control messages (e.g., health check responses).

### `main.cpp` Orchestration
- Parse CLI flags, wire dependencies, start threads, and supervise shutdown.
- Steps:
  1. Parse `--host`, `--sys-base`, `--data-base`, `--x`, `--n`, `--client-dir`.
  2. Initialize shared queue (`queue.hpp`) and system channels.
  3. Launch watcher thread feeding payloads into the compressor and chunker pipeline.
  4. Start sender threads consuming from the queue.
  5. Register signal handlers for graceful termination; on shutdown, close queues and join
     threads.
  6. Periodically report status via `system_channels.hpp`.

## Command Line Interface
| Flag | Description |
| --- | --- |
| `--host <hostname>` | Remote server host for data and system channels. |
| `--sys-base <port>` | Base TCP port for the four system channels (uses offsets 0..3). |
| `--data-base <port>` | Base TCP port for data sockets (number of ports equals `--x`). |
| `--x <count>` | Number of persistent data sockets to maintain concurrently. |
| `--n <parallelism>` | Maximum number of concurrent payloads processed by the pipeline. |
| `--client-dir <path>` | Directory watched for new payload files. |

