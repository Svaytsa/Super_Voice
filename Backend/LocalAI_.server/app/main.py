"""FastAPI application entrypoint for the LocalAI server."""

from __future__ import annotations

from contextlib import asynccontextmanager
from pathlib import Path
from typing import AsyncIterator

from fastapi import FastAPI

from .core.settings import Settings, get_settings
from .routers import api_router


def _ensure_directory(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


@asynccontextmanager
def lifespan(_: FastAPI) -> AsyncIterator[None]:
    """Application lifespan to prepare directories and resources."""

    settings = get_settings()
    for directory in settings.directories_to_ensure().values():
        _ensure_directory(directory)
    yield


def create_app() -> FastAPI:
    """Application factory for the LocalAI FastAPI service."""

    settings: Settings = get_settings()
    app = FastAPI(
        title="LocalAI Server",
        version="0.1.0",
        lifespan=lifespan,
    )
    app.state.settings = settings

    app.include_router(api_router)

    @app.get("/healthz", tags=["health"])
    async def healthz() -> dict[str, str]:
        return {"status": "ok"}

    return app


app = create_app()
