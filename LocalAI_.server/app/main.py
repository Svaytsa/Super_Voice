"""FastAPI application entrypoint for LocalAI server."""

from __future__ import annotations

from contextlib import asynccontextmanager
from pathlib import Path
from typing import AsyncIterator

from fastapi import FastAPI

from .core.settings import AppSettings, get_settings
from .routers import api_router


def ensure_directories(settings: AppSettings) -> None:
    """Ensure required directories exist for caching and model storage."""

    for directory in {settings.model_cache_dir, settings.hf_home}:
        Path(directory).mkdir(parents=True, exist_ok=True)


def create_lifespan(settings: AppSettings):
    """Create a lifespan context manager for the FastAPI application."""

    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        ensure_directories(settings)
        yield

    return lifespan


def create_app(settings: AppSettings | None = None) -> FastAPI:
    """Application factory for the LocalAI server."""

    app_settings = settings or get_settings()
    lifespan = create_lifespan(app_settings)

    app = FastAPI(title="LocalAI Server", lifespan=lifespan)
    app.state.settings = app_settings

    @app.get("/healthz", tags=["health"])
    async def healthcheck() -> dict[str, str]:
        return {"status": "ok"}

    app.include_router(api_router)

    return app


app = create_app()

__all__ = ["app", "create_app"]
