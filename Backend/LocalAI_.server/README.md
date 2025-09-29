# LocalAI Server

This repository hosts the FastAPI-based skeleton for the LocalAI multimodal inference service. The latest commit scaffolds the initial application structure: an application factory, dependency-free router registration, typed settings loading, and Pydantic schemas that describe the future multimodal contract.

## Getting Started

```bash
cd Backend/LocalAI_.server
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Environment variables can be configured in a `.env` file (see `.env.example`).

## Technical Overview

### Application entrypoint (`app/main.py`)
- Provides an application factory (`create_app`) that builds a FastAPI instance titled "LocalAI Server" version `0.1.0`.
- Declares a lifespan context manager that runs on startup to ensure model- and cache-related directories exist before serving requests.
- Attaches the global settings object to `app.state` for downstream access and mounts the root API router.
- Exposes a `/healthz` endpoint that reports `{ "status": "ok" }`, enabling infrastructure readiness probes.

### Configuration management (`app/core/settings.py`)
- Loads environment variables from `.env` (using `python-dotenv`) before instantiating settings.
- Defines a `Settings` Pydantic model with aliases for operational environment variables (host, port, log level, cache locations).
- Provides `directories_to_ensure()` to list filesystem paths that must exist; leveraged by the lifespan hook to create directories idempotently.
- Caches `get_settings()` via `functools.lru_cache` so configuration is constructed exactly once per process while still honouring environment overrides.

### Routing layer (`app/routers/__init__.py`)
- Declares a root `APIRouter` ready for future endpoint registration, keeping the routing surface centralized.

### Schema definitions (`app/schemas/common.py`)
- Supplies typed request and response contracts for multimodal operations, including:
  - `TextIn` for text generation requests with optional language and response format hints.
  - `AudioOut`, `ImageOut`, and `VideoOut` to describe rich media payload metadata.
  - `GenericResponse` wrapper that bundles generated content with timestamps and identifiers.

### Ancillary files
- `.env.example` documents configurable environment keys (host, port, logging, model cache directories).
- `pyproject.toml` pins FastAPI, Pydantic, and tooling dependencies to support application execution.

## Development Notes
- The current skeleton ships without business logic routers or service integrations; future work can extend `app/routers` and `app/services`.
- Because settings directories are created lazily at startup, ensure the configured paths are writable by the runtime environment.
- The schema module can be imported by future routers/services to guarantee consistent multimodal response shapes.
