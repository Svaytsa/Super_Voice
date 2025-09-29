from __future__ import annotations

from pydantic import BaseModel, Field

from .common import TimedResponse


class Txt2VidRequest(BaseModel):
    prompt: str
    negative_prompt: str | None = Field(default=None)
    num_frames: int | None = Field(default=None)
    num_inference_steps: int | None = Field(default=None)


class Txt2VidResponse(TimedResponse):
    video_base64: str | None = None
    note: str | None = None
    format: str | None = Field(default="mp4")


class Img2VidResponse(TimedResponse):
    video_base64: str | None = None
    note: str | None = None
    format: str | None = Field(default="mp4")
