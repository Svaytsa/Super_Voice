from __future__ import annotations

import io
import time
import uuid

from fastapi import APIRouter, File, Form, UploadFile

from ..schemas.images import Img2ImgResponse, Txt2ImgRequest, Txt2ImgResponse
from ..services.images import get_image_service
from ..utils import encode_bytes_to_base64, save_upload_to_temp

router = APIRouter(prefix="/v1/images", tags=["images"])


def _placeholder_image(prompt: str) -> str:
    from PIL import Image, ImageDraw  # type: ignore

    image = Image.new("RGB", (512, 512), color=(30, 30, 30))
    draw = ImageDraw.Draw(image)
    wrapped = "\n".join([prompt[i : i + 20] for i in range(0, len(prompt), 20)])
    draw.text((10, 10), wrapped or "No output", fill=(220, 220, 220))
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    return encode_bytes_to_base64(buffer.getvalue())


@router.post("/txt2img", response_model=Txt2ImgResponse)
async def text_to_image(payload: Txt2ImgRequest) -> Txt2ImgResponse:
    request_id = str(uuid.uuid4())
    start = time.perf_counter()
    service = get_image_service()
    image_base64 = service.txt2img(payload)
    if image_base64 is None:
        image_base64 = _placeholder_image(payload.prompt)
    duration_ms = (time.perf_counter() - start) * 1000.0
    return Txt2ImgResponse(
        request_id=request_id,
        duration_ms=duration_ms,
        image_base64=image_base64,
    )


@router.post("/img2img", response_model=Img2ImgResponse)
async def image_to_image(
    image_file: UploadFile = File(...),
    prompt: str | None = Form(default=None),
    strength: float = Form(default=0.75),
) -> Img2ImgResponse:
    request_id = str(uuid.uuid4())
    start = time.perf_counter()
    service = get_image_service()
    temp_path = save_upload_to_temp(image_file)
    try:
        image_base64 = service.img2img(temp_path, prompt, strength)
        if image_base64 is None:
            image_base64 = _placeholder_image(prompt or "")
    finally:
        temp_path.unlink(missing_ok=True)
    duration_ms = (time.perf_counter() - start) * 1000.0
    return Img2ImgResponse(
        request_id=request_id,
        duration_ms=duration_ms,
        image_base64=image_base64,
    )
