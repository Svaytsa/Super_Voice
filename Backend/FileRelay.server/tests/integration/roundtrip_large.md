# Integration Scenario: Large Payload Roundtrip

## Purpose
Stress the streaming, chunk assembly, and disk persistence paths with a large binary payload (2 MiB) and ensure checksum fidelity after end-to-end transfer.

## Preconditions
- Docker daemon with adequate CPU/network capacity.
- `./scripts/e2e.sh` available.
- Host has at least ~10 MiB free disk space for transient artifacts and logs.

## Execution Steps
1. Launch the harness for the large fixture (or allow the default matrix run to include it):
   ```bash
   ./scripts/e2e.sh --fixture roundtrip_large.bin
   ```
2. The script builds the Docker image if necessary and starts server/client containers on the host network.
3. Containers mount `data/e2e/server`, `data/e2e/client`, and `data/e2e/shared` for persistence and coordination.
4. When the client is ready, the harness materialises `roundtrip_large.bin` (2 097 152 bytes) inside the client watch directory.
5. The client streams compressed chunks to the server; the server reassembles them into `server_data/files/roundtrip_large.bin`.
6. Harness blocks until the reconstructed file exists, computes SHA-256, and compares it to the known-good digest.
7. Transfer timings and computed throughput are appended to `data/e2e/logs/metrics.csv`, with Docker logs collected for triage.

## Expected Results
- Server output appears at `data/e2e/server/files/roundtrip_large.bin`.
- SHA-256 digest equals `0b1ca26666e8dd13274c4d328d3e46b6403fb9c56c751091c5ce98ccff60bc04`.
- Metrics entry for `roundtrip_large.bin` shows finite duration and throughput.
- No critical errors appear in captured server or client logs.
