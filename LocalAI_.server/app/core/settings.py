"""Application configuration module."""

from __future__ import annotations

import os
from functools import lru_cache
from pathlib import Path
from typing import Any

from dotenv import load_dotenv
from pydantic import Field

try:  # Pydantic v2 recommends the separate pydantic-settings package.
    from pydantic_settings import BaseSettings, SettingsConfigDict
except ImportError:  # pragma: no cover - fallback if dependency is unavailable.
    from pydantic import BaseModel

    _HAS_PYDANTIC_SETTINGS = False

    class BaseSettings(BaseModel):
        """Lightweight fallback providing BaseSettings-like behaviour."""

        model_config: dict[str, Any] = {}

    def SettingsConfigDict(**kwargs: Any) -> dict[str, Any]:  # type: ignore
        return kwargs
else:  # pragma: no cover - executed when pydantic-settings is installed.
    _HAS_PYDANTIC_SETTINGS = True


load_dotenv()


class Settings(BaseSettings):
    """Centralised application settings."""

    app_host: str = Field("0.0.0.0", alias="APP_HOST")
    app_port: int = Field(8000, alias="APP_PORT")
    log_level: str = Field("info", alias="LOG_LEVEL")
    model_cache_dir: str = Field("/models", alias="MODEL_CACHE_DIR")
    hf_home: str = Field("/models/hf", alias="HF_HOME")

    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8", extra="ignore")

    @property
    def model_cache_path(self) -> Path:
        """Return the resolved model cache directory."""

        return Path(self.model_cache_dir).expanduser().resolve()

    @property
    def hf_home_path(self) -> Path:
        """Return the resolved Hugging Face cache directory."""

        return Path(self.hf_home).expanduser().resolve()

    @classmethod
    def from_env(cls) -> "Settings":
        """Create settings from environment variables when BaseSettings is unavailable."""

        if _HAS_PYDANTIC_SETTINGS:  # pragma: no cover - handled by BaseSettings automatically.
            return cls()

        return cls(
            app_host=os.getenv("APP_HOST", "0.0.0.0"),
            app_port=int(os.getenv("APP_PORT", "8000")),
            log_level=os.getenv("LOG_LEVEL", "info"),
            model_cache_dir=os.getenv("MODEL_CACHE_DIR", "/models"),
            hf_home=os.getenv("HF_HOME", "/models/hf"),
        )


@lru_cache
def get_settings() -> Settings:
    """Return cached settings instance."""

    return Settings.from_env()


settings = get_settings()
