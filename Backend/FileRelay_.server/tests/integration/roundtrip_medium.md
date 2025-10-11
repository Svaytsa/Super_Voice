# Integration Scenario: Medium Payload Roundtrip

## Purpose
Validate chunking, compression, and throughput behaviour for a medium-sized payload (512 KiB) transferred through the Dockerised stack.

## Preconditions
- Docker daemon is reachable and has sufficient disk space for temporary artifacts.
- `./scripts/e2e.sh` is executable.
- No leftover artifacts from previous runs exist under `data/e2e/` (the harness performs cleanup automatically but double-check when running manually).
- Legacy automation may still reference the `local-ai-model` image name. Retag it to `file-relay`
  or expose `LOCAL_AI_IMAGE` / `LOCAL_AI_MODEL_IMAGE`—the harness promotes those variables when
  `IMAGE_NAME` is not provided.

## Execution Steps
1. Invoke the harness for the medium tier:
   ```bash
   ./scripts/e2e.sh --fixture roundtrip_medium.bin
   ```
   or omit `--fixture` to run the full tiered matrix.
2. The script ensures the image is built, then starts the server container (host networking, mounted volumes under `data/e2e/server` and `data/e2e/shared`).
3. The client container boots, connects to the control/data channels, and waits for filesystem changes.
4. The harness synthesises `roundtrip_medium.bin` (524 288 bytes) in the mounted client directory, triggering the watcher.
5. Client uploads the Zstandard-compressed payload; the server reassembles and decompresses it into `server_data/files/roundtrip_medium.bin`.
6. Harness verifies SHA-256 integrity and logs transfer duration/throughput metrics alongside Docker logs.

## Expected Results
- Server output appears at `data/e2e/server/files/roundtrip_medium.bin`.
- SHA-256 digest matches `e112aebd828bef408fce72b834efe1f8c0d79c0df30eb2668655191f8eefd6f2`.
- Metrics CSV includes an entry for `roundtrip_medium.bin` with finite duration and throughput.
- No fatal errors are present in `data/e2e/logs/server.log` or `data/e2e/logs/client.log`.
