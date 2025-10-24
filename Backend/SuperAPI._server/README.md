# SuperAPI._server

SuperAPI._server is a production-ready Drogon-based HTTP service scaffolded with C++20, CMake, and modern tooling. It provides health, version, and metrics endpoints along with structured logging and configuration management via YAML and environment variables.

## Features

- C++20 with Drogon web framework (HTTP, WebSocket, SSE ready)
- Structured JSON logging and graceful shutdown hooks
- Configuration via `config/*.yaml` and environment variables (with optional `.env` file)
- Request ID middleware scaffold for per-request correlation
- Ready-to-extend Docker and docker-compose setup

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
- `config/providers.yaml` – External provider credentials and options

## Testing

Testing scaffolding is provided via CMake in `tests/`. Add your test targets there and integrate with your preferred framework.

## License

This scaffold is provided as-is. Integrate your organization licensing as appropriate.
