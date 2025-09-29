from __future__ import annotations

from pydantic import BaseModel, Field

from .common import TimedResponse


class LLMGenerateRequest(BaseModel):
    prompt: str = Field(..., description="Prompt to generate from")
    max_new_tokens: int | None = Field(default=128, ge=1, le=2048)
    temperature: float | None = Field(default=0.7, ge=0.0, le=2.0)
    top_p: float | None = Field(default=0.95, ge=0.0, le=1.0)


class LLMGenerateResponse(TimedResponse):
    generated_text: str
