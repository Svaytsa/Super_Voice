from __future__ import annotations

from pydantic import BaseModel, Field

from .common import TimedResponse


class Txt2ImgRequest(BaseModel):
    prompt: str
    negative_prompt: str | None = Field(default=None)
    num_inference_steps: int = Field(default=20, ge=1, le=100)
    guidance_scale: float = Field(default=7.5, ge=0.0, le=20.0)
    height: int | None = Field(default=None)
    width: int | None = Field(default=None)
    seed: int | None = Field(default=None)


class Txt2ImgResponse(TimedResponse):
    image_base64: str


class Img2ImgResponse(TimedResponse):
    image_base64: str
