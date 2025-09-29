"""Pydantic schemas shared across the LocalAI application."""

from .common import AudioOut, GenericResponse, ImageOut, TextIn, VideoOut

__all__ = [
    "AudioOut",
    "GenericResponse",
    "ImageOut",
    "TextIn",
    "VideoOut",
]
