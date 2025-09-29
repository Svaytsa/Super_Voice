"""Common Pydantic models shared across the API."""

from __future__ import annotations

from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, Field, HttpUrl


class TextIn(BaseModel):
    """Text prompt input for multimodal generation."""

    text: str = Field(..., description="Primary textual input.")
    context: str | None = Field(
        default=None,
        description="Optional contextual information that informs the generation pipeline.",
    )


class AudioOut(BaseModel):
    """Audio payload returned by the service."""

    mime_type: str = Field(..., description="MIME type describing the audio format.")
    content: str = Field(..., description="Base64 encoded audio data or a presigned URL.")
    duration_seconds: float | None = Field(
        default=None, description="Optional duration of the generated audio clip."
    )


class ImageOut(BaseModel):
    """Image payload returned by the service."""

    mime_type: str = Field(..., description="MIME type describing the image format.")
    content: str = Field(..., description="Base64 encoded image data or a presigned URL.")
    width: int | None = Field(default=None, ge=1, description="Image width in pixels.")
    height: int | None = Field(default=None, ge=1, description="Image height in pixels.")


class VideoOut(BaseModel):
    """Video payload returned by the service."""

    mime_type: str = Field(..., description="MIME type describing the video format.")
    content: str = Field(..., description="Base64 encoded video data or a presigned URL.")
    frame_rate: float | None = Field(
        default=None, description="Frame rate of the generated video in frames per second."
    )


class GenericResponse(BaseModel):
    """Generic metadata returned by model inference endpoints."""

    request_id: str = Field(..., description="Unique identifier for tracking the request.")
    status: Literal["success", "error"] = Field(
        "success", description="High level status reported by the service."
    )
    created_at: datetime = Field(
        default_factory=datetime.utcnow,
        description="Timestamp representing when the response was generated.",
    )
    model: str | None = Field(
        default=None, description="Identifier of the model that handled the request."
    )
    outputs: list[AudioOut | ImageOut | VideoOut | dict[str, Any]] | None = Field(
        default=None,
        description="Optional collection of multimodal outputs or additional payloads.",
    )
    metadata: dict[str, Any] | None = Field(
        default=None,
        description="Additional metadata supplied by the backend services.",
    )
    resource_uri: HttpUrl | None = Field(
        default=None, description="Location where the generated artifact can be retrieved."
    )
