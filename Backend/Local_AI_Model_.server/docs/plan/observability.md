# Observability Plan

## Goals

- Ensure end-to-end patch ingestion telemetry captures queue pressure, throughput, and retry behaviour.
- Maintain sustained client upload performance of **≥30 MB/s** with **≥4 parallel sockets** while keeping retry rates below 2%.
- Surface server-side assembly and cleanup activity to validate completeness and timely TTL enforcement.

## Metrics & Signals

### Client

- **QUEUE_SIZE_UPDATE** (system channel): published every ~500 ms with queue depth and capacity.
- **Throughput logs**: rolling 5-second summaries reporting queue utilisation, chunk/s, MB/s, and retry counts.
- **Retry logs**: per-chunk retry reasons including connection endpoint and attempt number.

### Server

- **Patch acceptance logs**: chunk identifier, size, and completeness percentage after storage.
- **Assembly logs**: publication path on successful rebuild or error diagnostics on failure.
- **TTL cleanup logs**: periodic sweeps that indicate the configured TTL horizon and files removed.

## Targets & Alerts

- Raise alerts if sustained throughput drops below 30 MB/s for more than 60 seconds with ≥4 sockets.
- Alert on retry rates exceeding 5% over a 1-minute window.
- Alert when completeness for any tracked payload stagnates (<100%) for longer than its TTL.

## Next Steps

- Integrate metrics with centralized log aggregation for retention and dashboards.
- Add structured logging output (JSON) for easier ingestion once the text stream is validated.
- Extend telemetry endpoints to expose the same metrics over HTTP for automated scraping.
