from __future__ import annotations

from pydantic import BaseModel, Field

from .common import TimedResponse


class TTSSynthesizeRequest(BaseModel):
    text: str = Field(..., description="Text to synthesize")
    speaker: str | None = Field(default=None, description="Speaker or voice ID")
    language: str | None = Field(default=None, description="Language code")


class TTSSynthesizeResponse(TimedResponse):
    audio_base64: str
    sample_rate: int
    format: str = Field(default="wav")
