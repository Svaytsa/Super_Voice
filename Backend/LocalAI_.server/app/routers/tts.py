from __future__ import annotations

import time
import uuid

from fastapi import APIRouter

from ..schemas.tts import TTSSynthesizeRequest, TTSSynthesizeResponse
from ..services.tts import get_tts_service

router = APIRouter(prefix="/v1/tts", tags=["tts"])


@router.post("/synthesize", response_model=TTSSynthesizeResponse)
async def synthesize_speech(payload: TTSSynthesizeRequest) -> TTSSynthesizeResponse:
    request_id = str(uuid.uuid4())
    start = time.perf_counter()
    service = get_tts_service()
    audio_base64, sample_rate = service.synthesize(payload)
    duration_ms = (time.perf_counter() - start) * 1000.0
    return TTSSynthesizeResponse(
        request_id=request_id,
        duration_ms=duration_ms,
        audio_base64=audio_base64,
        sample_rate=sample_rate,
    )
