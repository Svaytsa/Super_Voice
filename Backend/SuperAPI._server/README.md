# SuperAPI Server

SuperAPI is a Drogon-based C++20 service that fans out to multiple model providers while exposing a
stable, provider-namespaced contract. The project ships with batteries included for local
containerization, observability, and reproducible builds.

## Quick start

### Run with Docker Compose

1. Copy the sample environment file and update provider keys as needed:
   ```bash
   cp .env.example .env
   ```
2. Build and start the API:
   ```bash
   docker compose up --build
   ```
3. Verify the health and metrics endpoints:
   ```bash
   curl -s http://localhost:8080/health
   curl -s http://localhost:8080/metrics
   ```

> Tip: add the `observability` profile to launch the optional OpenTelemetry Collector, Prometheus,
> and Grafana stack:
> ```bash
> docker compose --profile observability up --build
> ```

### Build from source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --build build --target test  # optional: run unit tests
./build/superapi_server
```

### Run unit tests independently

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target superapi_tests
ctest --test-dir build --output-on-failure
```

The server listens on the configured host and port (default `0.0.0.0:8080`). Base endpoints:

- `GET /health`
- `GET /version`
- `GET /metrics`

## Configuration

All runtime configuration can be supplied through environment variables (see `.env.example`) or the
YAML files under `config/`. Environment values take precedence over YAML defaults.

### Core server settings

| Variable      | Default    | Description                                  |
| ------------- | ---------- | -------------------------------------------- |
| `HOST`        | `0.0.0.0`  | Listener address for incoming requests.       |
| `PORT`        | `8080`     | Listener port for HTTP traffic.              |
| `LOG_LEVEL`   | `info`     | Minimum log level (`trace` → `critical`).     |
| `DRY_RUN`     | `false`    | Skip outbound calls; respond with mock data. |

### Provider credentials and overrides

| Provider      | API key variable        | Optional base URL override                  |
| ------------- | ----------------------- | ------------------------------------------- |
| OpenAI        | `OPENAI_API_KEY`        | `OPENAI_BASE_URL` (default `https://api.openai.com/v1`) |
| Anthropic     | `ANTHROPIC_API_KEY`     | `ANTHROPIC_BASE_URL`                        |
| xAI           | `XAI_API_KEY`           | `XAI_BASE_URL`                              |
| Perplexity    | `PERPLEXITY_API_KEY`    | `PERPLEXITY_BASE_URL`                       |
| LLaMA         | `LAMA_API_KEY`          | `LAMA_BASE_URL`                             |
| Vertex AI     | `VERTEX_API_KEY`        | `VERTEX_BASE_URL`                           |
| Gemini        | `GEMINI_API_KEY`        | `GEMINI_BASE_URL`                           |
| Hugging Face  | `HUGGINGFACE_API_KEY`   | `HUGGINGFACE_BASE_URL`                      |
| OpenRouter    | `OPENROUTER_API_KEY`    | `OPENROUTER_BASE_URL`                       |
| AgentRouter   | `AGENTROUTER_API_KEY`   | `AGENTROUTER_BASE_URL`                      |
| DeepSeek      | `DEEPSEEK_API_KEY`      | `DEEPSEEK_BASE_URL`                         |
| Qwen          | `QWEN_API_KEY`          | `QWEN_BASE_URL`                             |
| Zhipu         | `ZHIPU_API_KEY`         | `ZHIPU_BASE_URL`                            |
| MiniMax       | `MINIMAX_API_KEY`       | `MINIMAX_BASE_URL`                          |

For OpenAI, include `OPENAI_ORG_ID` when scoped organization access is required.

### Observability

| Variable                     | Default                         | Description                                |
| --------------------------- | ------------------------------- | ------------------------------------------ |
| `OTEL_EXPORTER_OTLP_ENDPOINT` | `http://otel-collector:4317`   | OTLP collector endpoint.                   |
| `OTEL_EXPORTER_OTLP_HEADERS`  | _empty_                        | Extra OTLP headers (e.g., API keys).       |
| `GRAFANA_ADMIN_USER`          | `admin`                        | Grafana bootstrap username.                |
| `GRAFANA_ADMIN_PASSWORD`      | `admin`                        | Grafana bootstrap password.                |

## API overview

Every provider is exposed under a dedicated prefix. Replace `{provider}` with one of the namespaces
below to access aligned operations such as chat completions, embeddings, images, audio, models, and
jobs.

| Provider      | Namespace prefix | Notes |
| ------------- | ---------------- | ----- |
| OpenAI        | `/openai/...`    | Supports SSE streaming and tool calling. |
| Anthropic     | `/anthropic/...` | Claude-style chat completions. |
| xAI / Zhipu   | `/xai/...`       | Supply `vendor=grok` or `vendor=zhipu` via query or `X-Vendor`. |
| Perplexity    | `/perplexity/...` | Unified reasoning/chat endpoints. |
| LLaMA         | `/lama/...`      | Meta LLaMA REST compatibility. |
| Vertex        | `/vertex/...`    | Google Vertex AI REST compatibility. |
| Gemini        | `/gemini/...`    | Google Gemini REST compatibility. |
| Hugging Face  | `/huggingface/...` | Text + multimodal inference. |
| OpenRouter    | `/openrouter/...` | Bring-your-own model router support. |
| AgentRouter   | `/agentrouter/...` | Agent-first orchestration APIs. |
| DeepSeek      | `/deepseek/...`  | Chat + reasoning models. |
| Qwen          | `/qwen/...`      | Alibaba Qwen compatibility mode. |
| MiniMax       | `/minimax/...`   | MiniMax conversational models. |

Generated OpenAPI and JSON Schema artifacts live under `openapi/`. Lint them with the commands in
`openapi/README.md` or via the snippet below:

```bash
npx speccy lint openapi/superapi.openapi.yaml
npx @openapitools/openapi-generator-cli validate -i openapi/superapi.openapi.yaml
```

## Streaming example

Server-Sent Events (SSE) streaming is enabled across chat endpoints. The following example streams an
OpenAI-compatible response:

```bash
curl \
  -H "Authorization: Bearer $OPENAI_API_KEY" \
  -H "Content-Type: application/json" \
  -H "Accept: text/event-stream" \
  -N \
  -d '{
        "model": "gpt-4o-mini",
        "messages": [{"role": "user", "content": "Hello from SuperAPI"}],
        "stream": true
      }' \
  http://localhost:8080/openai/chat/completions
```

Each SSE event mirrors the OpenAI `delta` schema and terminates with a `done` event. WebSocket
endpoints emit the same payloads when negotiated.

## Observability stack

When launched with the `observability` profile, the compose stack provisions:

- **OpenTelemetry Collector**: receives OTLP spans/metrics from the API and exports to Prometheus.
- **Prometheus**: scrapes `superapi:8080/metrics` and the collector’s own metrics.
- **Grafana**: reachable at <http://localhost:3000> (defaults `admin/admin`). Point its Prometheus
  data source to `http://prometheus:9090` within the compose network or `http://localhost:9090` from
  the host.

## DRY_RUN mode

Set `DRY_RUN=true` to disable outbound provider traffic. The server logs a warning and returns mock
responses instead of invoking external APIs. This is useful for demos, contract testing, and CI
pipelines without credentials.

## Troubleshooting

- **Container exits immediately**: confirm the binary built successfully by re-running `docker compose
  up --build` and checking the build output.
- **`401 Unauthorized` responses**: ensure the corresponding provider API key is set in `.env` and the
  container was restarted.
- **Healthcheck failures**: verify the host machine is not already listening on port 8080 or change
  `PORT` in `.env` and rebuild.
- **Metrics not visible in Grafana**: start the stack with the `observability` profile and add the
  Prometheus data source in Grafana.

## Development workflow

- Format C++ sources with the bundled `.clang-format`:
  ```bash
  clang-format -i $(git ls-files '*.[ch]pp')
  ```
- Optional: install [`pre-commit`](https://pre-commit.com) hooks for formatting and linting:
  ```bash
  pip install pre-commit
  pre-commit install
  pre-commit run --all-files
  ```
- Run tests with CTest (`cmake --build build --target test`).

Structured JSON logs, metrics, and traces can be tailored via the YAML files in `config/` for more
advanced deployments.
