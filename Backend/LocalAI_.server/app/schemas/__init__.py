"""Pydantic schemas for LocalAI endpoints."""

from .common import ErrorResponse, TimedResponse
from .images import Img2ImgResponse, Txt2ImgRequest, Txt2ImgResponse
from .llm import LLMGenerateRequest, LLMGenerateResponse
from .stt import STTTranscribeResponse
from .tts import TTSSynthesizeRequest, TTSSynthesizeResponse
from .videos import Img2VidResponse, Txt2VidRequest, Txt2VidResponse
from .vlm import Image2TextResponse, Video2TextResponse

__all__ = [
    "ErrorResponse",
    "TimedResponse",
    "Img2ImgResponse",
    "Txt2ImgRequest",
    "Txt2ImgResponse",
    "LLMGenerateRequest",
    "LLMGenerateResponse",
    "STTTranscribeResponse",
    "TTSSynthesizeRequest",
    "TTSSynthesizeResponse",
    "Img2VidResponse",
    "Txt2VidRequest",
    "Txt2VidResponse",
    "Image2TextResponse",
    "Video2TextResponse",
]
