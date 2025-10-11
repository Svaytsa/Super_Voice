# Integration Scenario: Small Payload Roundtrip

## Purpose
Verify that the Dockerised client/server stack reliably transfers a small binary payload (4 KiB) from the client watch folder to the server storage, recreating the original file byte-for-byte.

## Preconditions
- Docker daemon available to the host running the test.
- `scripts/e2e.sh` accessible and executable.
- No conflicting containers named `file-relay-e2e-server` or `file-relay-e2e-client` are running.
- Legacy setups that previously used the `local-ai-model` image can either retag it to
  `file-relay` or export `LOCAL_AI_IMAGE` / `LOCAL_AI_MODEL_IMAGE`, which the harness treats as
  fallbacks when `IMAGE_NAME` is unset.

## Execution Steps
1. Run the end-to-end harness:
   ```bash
   ./scripts/e2e.sh --fixture roundtrip_small.bin
   ```
   If multiple fixtures are required, omit `--fixture` to exercise the full matrix.
2. The harness builds the `file-relay` image (unless cached) and launches dedicated server and client containers.
3. Once the client is idle, the harness generates `roundtrip_small.bin` (4 096 bytes) under the mounted client watch directory using the deterministic pattern `SuperVoiceTestPattern\n`.
4. The client uploads the compressed data to the server via the TCP data channel, which stores and reassembles it under `server_data/files/roundtrip_small.bin`.
5. The harness waits for the server-side artifact, computes its SHA-256 checksum, and compares it to the pre-computed expectation.
6. Final metrics (transfer duration and throughput) are appended to `data/e2e/logs/metrics.csv` together with container logs archived under `data/e2e/logs/`.

## Expected Results
- Server artifact appears at `data/e2e/server/files/roundtrip_small.bin`.
- SHA-256 digest equals `37069a6d24c6d8854fb37fcddbc06904f2cfe7a4cc21f72594493b0893d157f9`.
- Metrics CSV records a single row for `roundtrip_small.bin` with a non-zero throughput value.
- Server and client logs show no errors (non-critical informational messages are acceptable).
