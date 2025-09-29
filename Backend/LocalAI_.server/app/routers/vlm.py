from __future__ import annotations

import time
import uuid

from fastapi import APIRouter, File, UploadFile

from ..schemas.vlm import Image2TextResponse, Video2TextResponse
from ..services.vlm import get_vlm_service
from ..utils import save_upload_to_temp

router = APIRouter(prefix="/v1/vlm", tags=["vlm"])


@router.post("/image2text", response_model=Image2TextResponse)
async def image_to_text(image_file: UploadFile = File(...)) -> Image2TextResponse:
    request_id = str(uuid.uuid4())
    start = time.perf_counter()
    temp_path = save_upload_to_temp(image_file)
    service = get_vlm_service()
    try:
        caption = service.image_to_text(temp_path) or ""
    finally:
        temp_path.unlink(missing_ok=True)
    duration_ms = (time.perf_counter() - start) * 1000.0
    return Image2TextResponse(
        request_id=request_id,
        duration_ms=duration_ms,
        caption=caption,
    )


@router.post("/video2text", response_model=Video2TextResponse)
async def video_to_text(video_file: UploadFile = File(...)) -> Video2TextResponse:
    request_id = str(uuid.uuid4())
    start = time.perf_counter()
    temp_path = save_upload_to_temp(video_file)
    service = get_vlm_service()
    try:
        caption = service.video_to_text(temp_path) or ""
    finally:
        temp_path.unlink(missing_ok=True)
    duration_ms = (time.perf_counter() - start) * 1000.0
    return Video2TextResponse(
        request_id=request_id,
        duration_ms=duration_ms,
        caption=caption,
    )
