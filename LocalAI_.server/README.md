# LocalAI_.server

FastAPI-based skeleton for a multimodal inference API. This project provides the foundational structure for future development, including configuration management, schema definitions, and service placeholders.

## Getting Started

1. Create a virtual environment and install dependencies:
   ```bash
   pip install -e .
   ```
2. Copy `.env.example` to `.env` and adjust settings as needed.
3. Run the development server:
   ```bash
   uvicorn app.main:app --host 0.0.0.0 --port 8000
   ```

## Health Check

Verify the service is running:

```bash
curl -s http://localhost:8000/healthz
```
