from __future__ import annotations

from .common import TimedResponse


class Image2TextResponse(TimedResponse):
    caption: str


class Video2TextResponse(TimedResponse):
    caption: str
