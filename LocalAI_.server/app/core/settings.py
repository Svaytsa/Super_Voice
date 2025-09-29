"""Application settings module."""

from functools import lru_cache
from pathlib import Path
from typing import Any

from dotenv import load_dotenv
from pydantic import BaseSettings, Field


class AppSettings(BaseSettings):
    """Configuration for the LocalAI server application."""

    app_host: str = Field("0.0.0.0", alias="APP_HOST")
    app_port: int = Field(8000, alias="APP_PORT")
    log_level: str = Field("info", alias="LOG_LEVEL")
    model_cache_dir: Path = Field(Path("/models"), alias="MODEL_CACHE_DIR")
    hf_home: Path = Field(Path("/models/hf"), alias="HF_HOME")

    model_config = {
        "populate_by_name": True,
        "env_file": ".env",
        "env_file_encoding": "utf-8",
    }

    def as_dict(self) -> dict[str, Any]:
        """Return the settings as a serializable dictionary."""

        return {
            "app_host": self.app_host,
            "app_port": self.app_port,
            "log_level": self.log_level,
            "model_cache_dir": str(self.model_cache_dir),
            "hf_home": str(self.hf_home),
        }


@lru_cache(maxsize=1)
def get_settings() -> AppSettings:
    """Load and cache application settings."""

    load_dotenv()
    return AppSettings()


__all__ = ["AppSettings", "get_settings"]
