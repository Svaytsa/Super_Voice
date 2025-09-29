# LocalAI Server

This package provides the FastAPI-based skeleton for the LocalAI multimodal inference service. It exposes a health endpoint and wiring for configuration, schemas, and future routers/services.

## Getting Started

```bash
cd Backend/LocalAI_.server
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Environment variables can be configured in a `.env` file (see `.env.example`).
