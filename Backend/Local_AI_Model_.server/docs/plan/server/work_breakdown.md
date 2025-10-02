# Server Work Breakdown

## Scope
Define the modular responsibilities for the chunk-ingestion server that accepts client
connections, persists incoming payload pieces, assembles them into final artifacts, applies
runtime control commands, and continuously cleans outdated data based on TTL policies.

## Modules

### `listeners.hpp`
- **Responsibility:** Own listening sockets and connection dispatch for both system control
  and data transfer channels.
- **Operation:**
  - Instantiate four system-channel acceptors bound to `--sys-base` ports with offsets
    `0..3` (health, telemetry, control, ack).
  - Instantiate `X` data acceptors bound to sequential ports starting at `--data-base` for
    chunk payload streams.
  - For each accepted socket, hand off to the appropriate handler queues (control, telemetry,
    or data pipeline) without blocking the accept loop.
  - Surface non-fatal errors via logging; retry bind/accept with backoff rather than
    terminating the process.
- **Concurrency:** Dedicated thread per acceptor to maintain responsiveness and isolate data
  backpressure from system channels.

### `storage.hpp`
- **Responsibility:** Durable persistence of incoming patches and bookkeeping of payload IDs.
- **Layout:**
  - Store each payload under `patches/<file_id>/patch_<index>.bin` (binary chunk as received).
  - Maintain `patches/<file_id>/ids.list` containing committed chunk indexes for recovery.
- **Operation:**
  - Provide `store_chunk(file_id, index, total, span)` that writes the chunk atomically,
    updates `ids.list`, and returns completion status (all indexes `0..total-1` present?).
  - Track per-payload metadata (last update time, expected total) for TTL cleanup.
  - Expose enumeration helpers for assembler to request complete payloads and outstanding gaps.

### `assembler.hpp`
- **Responsibility:** Detect fully received payloads, assemble them, and materialize final
  files.
- **Operation:**
  - Verify completeness by ensuring all indexes `0..total-1` exist per payload metadata.
  - Concatenate patch files into a streaming `.part` artifact while validating chunk order and
    length.
  - Decompress the `.part` using Zstandard into `files/<original_name>.tmp` within a safe
    staging area.
  - Perform atomic `rename` from the staging `.tmp` path into `files/<original_name>` once
    decompression succeeds.
  - Remove `patches/<file_id>` directory (including `patch_*.bin` and `ids.list`) after a
    successful publish.
  - Emit progress / success events to system channels.

### `control.hpp`
- **Responsibility:** Process runtime commands arriving via system channels to adjust server
  behavior without restart.
- **Capabilities:**
  - Update the number of active data acceptors `X` (scale up/down listeners and worker pools
    safely, ensuring graceful drain before closure).
  - Change the retention TTL `N` used by storage cleanup routines (propagated atomically to
    running timers).
  - Provide acknowledgements / error responses to the client control channel.

### `main.cpp`
- **Responsibility:** Entry point orchestrating subsystems, supervising threads, and ensuring
  resilience.
- **Operation:**
  - Parse CLI flags (`--sys-base`, `--data-base`, `--x`, `--ttl`, `--files-dir`, etc.).
  - Initialize shared services (storage, assembler, control) and spawn listener threads.
  - Launch worker loops to read from data sockets, persist chunks, and trigger assembly on
    completion.
  - Schedule periodic cleanup passes based on the current TTL for both incomplete and
    completed payload artifacts.
  - Capture exceptions/errors from threads, log them, and attempt recovery or restart of the
    affected component without exiting the process.
  - Coordinate graceful shutdown on signal: stop acceptors, flush queues, finalize assembly.

## Background Cleanup
- Run a scheduled task (e.g., every minute) that consults storage metadata for last activity
  timestamps.
- Remove incomplete payloads whose age exceeds TTL `N` (delete their patch directories).
- Remove completed payload outputs in `files/` that exceed TTL `N`, along with any lingering
  temporary files.
- Emit telemetry about cleanup actions via system channels for monitoring.
