from __future__ import annotations

from .common import TimedResponse


class STTTranscribeResponse(TimedResponse):
    text: str
    language: str | None = None
    duration_seconds: float | None = None
