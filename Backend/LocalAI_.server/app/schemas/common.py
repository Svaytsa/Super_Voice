"""Common request and response schemas for multimodal inference."""

from datetime import datetime
from typing import Literal, Optional

from pydantic import BaseModel, Field


class TextIn(BaseModel):
    """Input schema for text generation requests."""

    prompt: str = Field(..., description="User prompt for the model")
    language: Optional[str] = Field(
        default=None,
        description="Optional language hint for the model to respond in.",
    )
    response_format: Literal["text", "json"] = Field(
        default="text",
        description="Desired format of the generated response.",
    )


class AudioOut(BaseModel):
    """Representation of generated audio content."""

    mime_type: str = Field(default="audio/wav", description="MIME type of the audio data")
    url: Optional[str] = Field(
        default=None,
        description="Optional remote location where the audio can be downloaded.",
    )
    transcript: Optional[str] = Field(
        default=None,
        description="Optional transcript associated with the generated audio.",
    )


class ImageOut(BaseModel):
    """Representation of generated image content."""

    mime_type: str = Field(default="image/png", description="MIME type of the image data")
    url: Optional[str] = Field(
        default=None,
        description="Optional remote location of the generated image.",
    )
    width: Optional[int] = Field(default=None, description="Width of the generated image.")
    height: Optional[int] = Field(default=None, description="Height of the generated image.")


class VideoOut(BaseModel):
    """Representation of generated video content."""

    mime_type: str = Field(default="video/mp4", description="MIME type of the video data")
    url: Optional[str] = Field(
        default=None,
        description="Optional remote location of the generated video.",
    )
    duration_seconds: Optional[float] = Field(
        default=None,
        description="Duration in seconds of the generated video clip.",
    )


class GenericResponse(BaseModel):
    """Generic multimodal response wrapper."""

    request_id: str = Field(..., description="Unique identifier for the inference request.")
    created_at: datetime = Field(
        default_factory=datetime.utcnow,
        description="Timestamp when the response was generated.",
    )
    text: Optional[str] = Field(default=None, description="Generated text response, if any.")
    audio: Optional[AudioOut] = Field(
        default=None,
        description="Generated audio payload, when available.",
    )
    image: Optional[ImageOut] = Field(
        default=None,
        description="Generated image payload, when available.",
    )
    video: Optional[VideoOut] = Field(
        default=None,
        description="Generated video payload, when available.",
    )
