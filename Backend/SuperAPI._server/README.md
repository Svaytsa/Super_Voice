# SuperAPI._server

SuperAPI._server is a production-ready Drogon-based HTTP service scaffolded with C++20, CMake, and modern tooling. It provides health, version, and metrics endpoints along with structured logging and configuration management via YAML and environment variables.

## Features

- C++20 with Drogon web framework (HTTP, WebSocket, SSE ready)
- Structured JSON logging and graceful shutdown hooks
- Configuration via `config/*.yaml` and environment variables (with optional `.env` file)
- Request ID middleware scaffold for per-request correlation
- Ready-to-extend Docker and docker-compose setup

## API Overview

The SuperAPI contract publishes OpenAI-aligned request and response envelopes while enforcing
strict vendor namespacing. Each provider is addressed through a fixed prefix—there is no
auto-routing or provider inference. Current namespaces:

- `/openai/...`
- `/anphropic/...`
- `/xai/...` (shared by Grok and Zhipu)
- `/perplexety/...`
- `/lama/...`
- `/vertex/...`
- `/gemini/...`
- `/huggingface/...`
- `/openrouter/...`
- `/agentrouter/...`
- `/deepseek/...`
- `/qwen/...`
- `/minimax/...`

Each namespace exposes unified endpoints for chat completions, embeddings, image/video
generation, speech synthesis, audio transcription, model catalog, batch ingestion, and job
inspection. Streaming is available via `text/event-stream`, with equivalent WebSocket event
envelopes (`delta`, `tool_call`, `error`, `done`).

> **xAI & Zhipu routing**: The `/xai/...` namespace is shared. Clients **must** declare the
> target vendor using either the required `vendor={grok|zhipu}` query parameter or the
> `X-Vendor: grok|zhipu` header. Requests lacking an explicit selector are rejected; the
> platform never guesses a provider.

### Contract artifacts

Generated OpenAPI documents and JSON Schemas live under `openapi/`. A unified
`superapi.openapi.yaml` aggregates every namespace, and per-vendor overlays exist in
`openapi/companies/*.yaml`. Canonical schema definitions reside in `openapi/schemas/` and are
referenced by every specification.

### Validating the artifacts

Use the following commands from the repository root to lint the specifications and compile
the JSON Schemas:

```bash
# OpenAPI validation (lint + structural validation)
npx speccy lint Backend/SuperAPI._server/openapi/superapi.openapi.yaml
npx @openapitools/openapi-generator-cli validate \
  -i Backend/SuperAPI._server/openapi/superapi.openapi.yaml

# Validate each per-company contract
for file in Backend/SuperAPI._server/openapi/companies/*.yaml; do
  npx speccy lint "$file"
done

# Compile every JSON Schema with AJV (draft 2020-12)
for schema in Backend/SuperAPI._server/openapi/schemas/*.json; do
  npx ajv-cli compile --spec=draft2020 -s "$schema"
done
```

## Getting Started

### Prerequisites

- CMake >= 3.20
- A C++20-compatible compiler (GCC 11+, Clang 12+, or MSVC 2019+)
- Git and build essentials

### Configure environment

Copy `.env.example` to `.env` and adjust as needed. Environment variables override YAML configuration where applicable.

```bash
cp .env.example .env
```

### Build

```bash
cmake -S . -B build
cmake --build build -j
```

### Run

```bash
./build/superapi_server
```

The server listens on the configured host and port. Base endpoints:

- `GET /health`
- `GET /version`
- `GET /metrics`

## Observability

The service exposes structured logs, Prometheus metrics, and OpenTelemetry traces for every request.

### Logging

- Logs are emitted in JSON with the fields `ts`, `level`, `msg`, `request_id`, `company`, `endpoint`, `status`, and `latency_ms`.
- Custom redaction patterns can be configured in `config/logging.yaml` under `logging.redact.patterns` in addition to built-in redaction of email addresses, phone numbers, payment card numbers, and API tokens.
- A `X-Request-ID` header is required on every response and can be supplied by clients to correlate logs.

### Metrics

Metrics are exposed via `/metrics` in Prometheus text format and include per-company/endpoint cardinality for:

- `requests_total` (counter)
- `errors_total{type="http_4xx"|"http_5xx"|...}`
- `latency_ms_bucket`, `latency_ms_sum`, `latency_ms_count`
- `bytes_in`, `bytes_out`
- `tokens_in`, `tokens_out`
- `stream_events_total`

Check locally:

```bash
curl -s http://localhost:8080/metrics
```

### Tracing

- Incoming requests start an OpenTelemetry span with context extracted from `traceparent` headers; downstream calls can reuse `Tracer::startSpan` to create children.
- Spans automatically propagate `traceparent` in responses.
- Traces are exported via OTLP (gRPC/HTTP) using `config/otel.yaml` or the `OTEL_EXPORTER_*` environment variables.

### Local Prometheus + Grafana stack

An optional docker-compose overlay is provided to run Prometheus, Grafana, and an OpenTelemetry Collector locally:

```bash
docker compose -f docker-compose.yml -f docker-compose.observability.yml up
```

- Prometheus: <http://localhost:9090>
- Grafana: <http://localhost:3000> (credentials `admin` / `admin` by default)
- OTLP receiver: `http://localhost:4317` and `http://localhost:4318`

Grafana ships without pre-provisioned dashboards; add Prometheus as a data source pointed at `http://prometheus:9090` inside the compose network or `http://localhost:9090` from the host.

### High-concurrency smoke test

Use `hey` or a similar tool to verify throughput and latency while metrics and traces remain stable:

```bash
hey -z 30s -c 50 http://localhost:8080/health
```

## Streaming & Media

Completion endpoints accept `stream=true`. When `Accept: text/event-stream` (or `transport=sse`) is supplied, SuperAPI returns
Server-Sent Events with `delta` updates and a final `done` payload; each frame is JSON encoded. Example:

```bash
curl -N \
  -H "Accept: text/event-stream" \
  -H "Content-Type: application/json" \
  -d '{"model":"gpt-4o-mini","messages":[{"role":"user","content":"hello"}],"stream":true}' \
  http://localhost:18080/openai/chat/completions
```

WebSocket upgrades use `transport=websocket`. The handshake is currently rejected with a structured `unsupported_transport`
error while bidirectional frames are under active development; prefer SSE today.

```bash
wscat -c 'ws://localhost:18080/openai/chat/completions?stream=true&transport=websocket'
```

Speech recognition accepts JSON or `multipart/form-data` uploads under `/audio/transcriptions`. Attach the audio blob as the
`file` field alongside optional text inputs. `/audio/speech` streams synthesized audio bytes (`audio/wav` by default, switch to
MPEG with `Accept: audio/mpeg`) using chunked transfer.

### Docker

Build and run the service with Docker:

```bash
docker compose build
docker compose up
```

## Configuration

Key configuration files:

- `config/server.yaml` – Drogon server configuration defaults
- `config/logging.yaml` – Logging preferences and severity
- `config/otel.yaml` – OpenTelemetry exporter placeholders
- `config/observability/*` – Local Prometheus and OpenTelemetry Collector configs
- `config/providers.yaml` – External provider credentials and options

## Testing

Testing scaffolding is provided via CMake in `tests/`. Add your test targets there and integrate with your preferred framework.

## License

This scaffold is provided as-is. Integrate your organization licensing as appropriate.
