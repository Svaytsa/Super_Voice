from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import Optional, Tuple

from ..core.config import Settings, get_settings


class STTService:
    def __init__(self, settings: Settings):
        self._settings = settings
        self._model = None

    def _ensure_model(self) -> None:
        if self._model is not None:
            return
        try:
            from faster_whisper import WhisperModel  # type: ignore

            compute_type = "float16"
            try:
                import torch  # type: ignore

                if not torch.cuda.is_available():
                    compute_type = "int8"
            except Exception:
                compute_type = "int8"
            self._model = WhisperModel(
                self._settings.stt_model_id,
                device="cuda" if compute_type == "float16" else "cpu",
                compute_type=compute_type,
            )
        except Exception:
            self._model = None

    def transcribe(self, audio_path: Path) -> Tuple[str, Optional[str], Optional[float]]:
        self._ensure_model()
        if self._model is None:
            return ("", None, None)
        try:
            segments, info = self._model.transcribe(str(audio_path))
            text = " ".join(segment.text.strip() for segment in segments).strip()
            return text, getattr(info, "language", None), getattr(info, "duration", None)
        except Exception:
            return ("", None, None)


@lru_cache()
def get_stt_service(settings: Optional[Settings] = None) -> STTService:
    return STTService(settings or get_settings())
