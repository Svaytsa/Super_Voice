# LocalAI Server

This package contains the FastAPI skeleton for a multimodal inference service. It exposes a health check endpoint and provides a foundation for wiring multimodal pipelines in subsequent iterations.

## Getting started

1. Create a virtual environment and install dependencies:

   ```bash
   python -m venv .venv
   source .venv/bin/activate
   pip install -e .
   ```

2. Configure environment variables by copying the example file:

   ```bash
   cp .env.example .env
   ```

3. Launch the development server:

   ```bash
   uvicorn app.main:app --host 0.0.0.0 --port 8000
   ```

4. Verify the health endpoint:

   ```bash
   curl -s http://localhost:8000/healthz
   ```

## Project structure

```
app/
├── core/
│   └── settings.py
├── main.py
├── routers/
│   └── __init__.py
├── schemas/
│   ├── __init__.py
│   └── common.py
└── services/
    └── __init__.py
```

The skeleton loads configuration from environment variables, prepares model cache directories during startup, and provides reusable schemas for multimodal responses.
