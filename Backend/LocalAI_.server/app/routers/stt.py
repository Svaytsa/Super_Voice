from __future__ import annotations

import time
import uuid

from fastapi import APIRouter, File, UploadFile

from ..schemas.stt import STTTranscribeResponse
from ..services.stt import get_stt_service
from ..utils import save_upload_to_temp

router = APIRouter(prefix="/v1/stt", tags=["stt"])


@router.post("/transcribe", response_model=STTTranscribeResponse)
async def transcribe_audio(audio_file: UploadFile = File(...)) -> STTTranscribeResponse:
    request_id = str(uuid.uuid4())
    start = time.perf_counter()
    temp_path = save_upload_to_temp(audio_file, suffix=".wav")
    service = get_stt_service()
    text, language, duration_seconds = service.transcribe(temp_path)
    temp_path.unlink(missing_ok=True)
    duration_ms = (time.perf_counter() - start) * 1000.0
    return STTTranscribeResponse(
        request_id=request_id,
        duration_ms=duration_ms,
        text=text,
        language=language,
        duration_seconds=duration_seconds,
    )
