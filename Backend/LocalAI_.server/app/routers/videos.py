from __future__ import annotations

import time
import uuid

from fastapi import APIRouter, File, Form, UploadFile

from ..schemas.videos import Img2VidResponse, Txt2VidRequest, Txt2VidResponse
from ..services.videos import get_video_service
from ..utils import save_upload_to_temp

router = APIRouter(prefix="/v1/videos", tags=["videos"])


def _parse_optional_int(value: str | None) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value)
    except ValueError:
        return None


@router.post("/txt2vid", response_model=Txt2VidResponse)
async def text_to_video(payload: Txt2VidRequest) -> Txt2VidResponse:
    request_id = str(uuid.uuid4())
    start = time.perf_counter()
    service = get_video_service()
    video_base64, note, fmt = service.txt2vid(payload)
    duration_ms = (time.perf_counter() - start) * 1000.0
    return Txt2VidResponse(
        request_id=request_id,
        duration_ms=duration_ms,
        video_base64=video_base64,
        note=note,
        format=fmt,
    )


@router.post("/img2vid", response_model=Img2VidResponse)
async def image_to_video(
    image_file: UploadFile = File(...),
    prompt: str = Form(...),
    negative_prompt: str | None = Form(default=None),
    num_frames: str | None = Form(default=None),
    num_inference_steps: str | None = Form(default=None),
) -> Img2VidResponse:
    request_id = str(uuid.uuid4())
    start = time.perf_counter()
    service = get_video_service()
    temp_path = save_upload_to_temp(image_file)
    request = Txt2VidRequest(
        prompt=prompt,
        negative_prompt=negative_prompt,
        num_frames=_parse_optional_int(num_frames),
        num_inference_steps=_parse_optional_int(num_inference_steps),
    )
    try:
        video_base64, note, fmt = service.img2vid(temp_path, request)
    finally:
        temp_path.unlink(missing_ok=True)
    duration_ms = (time.perf_counter() - start) * 1000.0
    return Img2VidResponse(
        request_id=request_id,
        duration_ms=duration_ms,
        video_base64=video_base64,
        note=note,
        format=fmt,
    )
