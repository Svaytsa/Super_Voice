# mcp-cron Schedule: File Relay Server

This document defines the `mcp-cron` automation that continuously exercises the Docker image build and end-to-end (E2E) roundtrip tests described in `scripts/e2e.sh`.

## Environment
- **Repository:** `Super_Voice/Backend/FileRelay.server`
- **Runner image:** Ubuntu 22.04 with Docker CLI, Python 3.10+, and `mcp-cron` agent.
- **Working directory:** Repository root (`/workspace/Super_Voice/Backend/FileRelay.server`).
- **Shared cache directories:**
  - `/var/cache/file-relay/docker` for Docker layer cache (optional).
  - `/var/cache/file-relay/artifacts` for persisted logs and metrics.

## Jobs

### 1. Nightly Build & Lint
Runs every night at 01:00 UTC to ensure the container image is still buildable and the codebase compiles cleanly.

```yaml
jobs:
  nightly-build:
    schedule: "0 1 * * *"
    description: "Build Docker image and run unit targets"
    checkout: true
    steps:
      - name: Restore docker cache
        run: |
          docker load < /var/cache/file-relay/docker/cache.tar || true
      - name: Build release image
        run: |
          docker build -t file-relay .
      - name: Persist docker cache
        run: |
          mkdir -p /var/cache/file-relay/docker
          docker save file-relay > /var/cache/file-relay/docker/cache.tar
      - name: Smoke test binaries
        run: |
          cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
          cmake --build build --target client_app server_app --config Release
      - name: Archive build logs
        artifacts:
          paths:
            - build/**/*.log
          destination: nightly-build
```

### 2. Daily E2E (Small & Medium Fixtures)
Runs every day at 04:00 UTC to exercise the lightweight fixtures and detect regressions quickly.

```yaml
  daily-e2e:
    schedule: "0 4 * * *"
    description: "Run E2E harness for small and medium fixtures"
    checkout: true
    env:
      IMAGE_NAME: file-relay-nightly
      E2E_ROOT: /var/cache/file-relay/e2e
    steps:
      - name: Restore docker cache
        run: |
          docker load < /var/cache/file-relay/docker/cache.tar || true
      - name: Execute harness (small + medium)
        run: |
          ./scripts/e2e.sh --fixture roundtrip_small.bin --no-build
          ./scripts/e2e.sh --fixture roundtrip_medium.bin --no-build
      - name: Collect artifacts
        artifacts:
          paths:
            - data/e2e/logs/*.log
            - data/e2e/logs/metrics.csv
          destination: daily-e2e
```

### 3. Weekly Full Matrix (All Fixtures)
Runs every Sunday at 05:30 UTC with the complete roundtrip matrix, including the large payload.

```yaml
  weekly-e2e:
    schedule: "30 5 * * 0"
    description: "Run full E2E matrix (small, medium, large)"
    checkout: true
    env:
      IMAGE_NAME: file-relay-weekly
      E2E_ROOT: /var/cache/file-relay/e2e
    steps:
      - name: Rebuild image (fresh layer)
        run: |
          docker build --pull -t ${IMAGE_NAME} .
      - name: Run complete matrix
        run: |
          ./scripts/e2e.sh --no-build
      - name: Upload artifacts
        artifacts:
          paths:
            - data/e2e/logs/*.log
            - data/e2e/logs/metrics.csv
          destination: weekly-e2e
      - name: Publish summary
        run: |
          tail -n +2 data/e2e/logs/metrics.csv | awk -F, '{printf "%-20s %8.2f MiB %8.2f s %8.2f MiB/s\n", $1, $2/1048576, $5/1000, $6}'
```

## Notifications
- On success: update the `mcp-cron` dashboard entry for the respective job.
- On failure: trigger the default alert channel with links to archived logs.
- Repeated failures (≥2 consecutive) escalate to the on-call rotation.

## Artifact Retention
- Keep the last 14 nightly build logs.
- Keep the last 7 daily E2E logs.
- Keep the last 4 weekly E2E log bundles.

## Future Enhancements
- Add matrix expansion for alternative compression levels once available.
- Integrate Prometheus pushgateway for richer timing metrics.
- Surface SHA mismatch counts as dedicated alert metrics.
