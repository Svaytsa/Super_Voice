"""Entry point for the LocalAI FastAPI application."""

from __future__ import annotations

from contextlib import asynccontextmanager
from pathlib import Path
from typing import Iterable

from fastapi import FastAPI

from .core.settings import Settings, get_settings
from .routers import api_router


def _ensure_directories(directories: Iterable[Path]) -> None:
    """Create required directories if they do not already exist."""

    for directory in directories:
        directory.mkdir(parents=True, exist_ok=True)


def create_app(settings: Settings | None = None) -> FastAPI:
    """FastAPI application factory."""

    app_settings = settings or get_settings()

    @asynccontextmanager
    async def lifespan(app: FastAPI):  # pragma: no cover - executed at runtime.
        _ensure_directories({app_settings.model_cache_path, app_settings.hf_home_path})
        yield

    app = FastAPI(title="LocalAI Server", lifespan=lifespan)
    app.include_router(api_router)

    @app.get("/healthz", tags=["system"])
    async def healthz() -> dict[str, str]:
        """Return a simple health check payload."""

        return {"status": "ok"}

    return app


app = create_app()

__all__ = ["app", "create_app"]
