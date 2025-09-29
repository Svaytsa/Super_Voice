"""Common request and response schemas for multimodal inference."""

from __future__ import annotations

from datetime import datetime
from typing import Any, Dict, List, Optional

from pydantic import BaseModel, Field


class TextIn(BaseModel):
    """Schema representing a generic text-based prompt."""

    text: str = Field(..., description="Input text or prompt used for generation.")
    metadata: Optional[Dict[str, Any]] = Field(
        default=None,
        description="Optional metadata describing the text input context.",
    )


class AudioOut(BaseModel):
    """Schema for audio generation outputs."""

    data: str = Field(..., description="Base64-encoded audio payload.")
    mime_type: str = Field("audio/wav", description="MIME type of the encoded audio.")
    sample_rate: Optional[int] = Field(
        default=None, description="Sample rate in Hz for the generated audio."
    )


class ImageOut(BaseModel):
    """Schema for image generation outputs."""

    data: str = Field(..., description="Base64-encoded image payload.")
    mime_type: str = Field("image/png", description="MIME type of the encoded image.")
    width: Optional[int] = Field(default=None, description="Width of the image in pixels.")
    height: Optional[int] = Field(default=None, description="Height of the image in pixels.")


class VideoOut(BaseModel):
    """Schema for video generation outputs."""

    data: str = Field(..., description="Base64-encoded video payload.")
    mime_type: str = Field("video/mp4", description="MIME type of the encoded video.")
    duration: Optional[float] = Field(
        default=None, description="Duration of the generated video in seconds."
    )
    frame_rate: Optional[float] = Field(
        default=None, description="Frame rate of the generated video."
    )


class GenericResponse(BaseModel):
    """Standard response envelope for inference endpoints."""

    request_id: str = Field(..., description="Identifier correlating to the original request.")
    created_at: datetime = Field(
        default_factory=datetime.utcnow, description="Timestamp of the response creation."
    )
    outputs: Optional[List[Any]] = Field(
        default=None,
        description="Collection of generated outputs across modalities (audio/image/video).",
    )
    metadata: Optional[Dict[str, Any]] = Field(
        default=None, description="Additional metadata returned from the inference pipeline."
    )


__all__ = [
    "TextIn",
    "AudioOut",
    "ImageOut",
    "VideoOut",
    "GenericResponse",
]
