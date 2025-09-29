"""Service factories for LocalAI server."""

from .images import get_image_service
from .llm import get_llm_service
from .stt import get_stt_service
from .tts import get_tts_service
from .videos import get_video_service
from .vlm import get_vlm_service

__all__ = [
    "get_image_service",
    "get_llm_service",
    "get_stt_service",
    "get_tts_service",
    "get_video_service",
    "get_vlm_service",
]
