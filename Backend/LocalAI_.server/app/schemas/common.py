from __future__ import annotations

from pydantic import BaseModel


class TimedResponse(BaseModel):
    request_id: str
    duration_ms: float


class ErrorResponse(TimedResponse):
    error: str
