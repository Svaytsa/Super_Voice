from __future__ import annotations

import math
import struct
import wave
from functools import lru_cache
from pathlib import Path
from typing import Optional, Tuple

from ..core.config import Settings, get_settings
from ..schemas.tts import TTSSynthesizeRequest
from ..utils import encode_file_to_base64


class TTSService:
    def __init__(self, settings: Settings):
        self._settings = settings
        self._tts = None

    def _ensure_model(self) -> None:
        if self._tts is not None:
            return
        model_id = self._settings.tts_model_id
        try:
            from TTS.api import TTS  # type: ignore

            self._tts = TTS(model_name=model_id)
        except Exception:
            self._tts = None

    def synthesize(self, request: TTSSynthesizeRequest) -> Tuple[str, int]:
        self._ensure_model()
        if self._tts is None:
            return self._generate_fallback_audio(request.text)
        from tempfile import NamedTemporaryFile

        with NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            file_path = Path(tmp.name)
        try:
            self._tts.tts_to_file(
                text=request.text,
                file_path=str(file_path),
                speaker=request.speaker,
                language=request.language,
            )
            audio_base64 = encode_file_to_base64(file_path)
            sample_rate = getattr(self._tts, "output_sample_rate", 22050)
            return audio_base64, int(sample_rate)
        except Exception:
            return self._generate_fallback_audio(request.text)
        finally:
            if file_path.exists():
                file_path.unlink(missing_ok=True)

    def _generate_fallback_audio(self, text: str) -> Tuple[str, int]:
        duration = max(0.5, min(len(text) / 20.0, 5.0))
        sample_rate = 22050
        frequency = 440.0
        num_samples = int(sample_rate * duration)
        from tempfile import NamedTemporaryFile

        with NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            file_path = Path(tmp.name)
        with wave.open(str(file_path), "wb") as wav_file:
            wav_file.setnchannels(1)
            wav_file.setsampwidth(2)
            wav_file.setframerate(sample_rate)
            for i in range(num_samples):
                value = int(32767.0 * math.sin(2 * math.pi * frequency * i / sample_rate))
                wav_file.writeframes(struct.pack("<h", value))
        audio_base64 = encode_file_to_base64(file_path)
        file_path.unlink(missing_ok=True)
        return audio_base64, sample_rate


@lru_cache()
def get_tts_service(settings: Optional[Settings] = None) -> TTSService:
    return TTSService(settings or get_settings())
