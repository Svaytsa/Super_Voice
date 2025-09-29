from __future__ import annotations

import time
import uuid

from fastapi import APIRouter

from ..schemas.llm import LLMGenerateRequest, LLMGenerateResponse
from ..services.llm import get_llm_service

router = APIRouter(prefix="/v1/llm", tags=["llm"])


@router.post("/generate", response_model=LLMGenerateResponse)
async def generate_text(payload: LLMGenerateRequest) -> LLMGenerateResponse:
    request_id = str(uuid.uuid4())
    start = time.perf_counter()
    service = get_llm_service()
    generated_text = service.generate(payload)
    duration_ms = (time.perf_counter() - start) * 1000.0
    return LLMGenerateResponse(
        request_id=request_id,
        duration_ms=duration_ms,
        generated_text=generated_text,
    )
