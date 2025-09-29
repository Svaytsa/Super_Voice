"""FastAPI routers for LocalAI server."""

from . import images, llm, stt, tts, videos, vlm

__all__ = [
    "images",
    "llm",
    "stt",
    "tts",
    "videos",
    "vlm",
]
