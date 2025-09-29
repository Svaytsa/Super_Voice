"""Application settings management using Pydantic models."""

from __future__ import annotations

import os
from functools import lru_cache
from pathlib import Path
from typing import Dict

from dotenv import load_dotenv
from pydantic import BaseModel, Field

load_dotenv()


class Settings(BaseModel):
    """Runtime configuration for the LocalAI server."""

    app_host: str = Field(default="0.0.0.0", alias="APP_HOST")
    app_port: int = Field(default=8000, alias="APP_PORT")
    log_level: str = Field(default="info", alias="LOG_LEVEL")
    model_cache_dir: Path = Field(default=Path("/models"), alias="MODEL_CACHE_DIR")
    hf_home: Path = Field(default=Path("/models/hf"), alias="HF_HOME")

    model_config = {
        "populate_by_name": True,
        "arbitrary_types_allowed": True,
    }

    def directories_to_ensure(self) -> Dict[str, Path]:
        """Return the directories that should exist before serving requests."""

        return {
            "model_cache_dir": self.model_cache_dir,
            "hf_home": self.hf_home,
        }


def _environment_overrides() -> Dict[str, object]:
    overrides: Dict[str, object] = {}
    for field_name, field in Settings.model_fields.items():
        env_key = field.alias or field_name.upper()
        raw_value = os.getenv(env_key)
        if raw_value is not None:
            overrides[field_name] = raw_value
    return overrides


@lru_cache()
def get_settings() -> Settings:
    """Load application settings from environment variables once per process."""

    overrides = _environment_overrides()
    return Settings(**overrides)


__all__ = ["Settings", "get_settings"]
