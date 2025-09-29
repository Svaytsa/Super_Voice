from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import Optional, Tuple

from ..core.config import Settings, get_settings
from ..schemas.videos import Txt2VidRequest
from ..utils import encode_file_to_base64


class VideoService:
    def __init__(self, settings: Settings):
        self._settings = settings
        self._txt2vid_pipe = None
        self._img2vid_pipe = None

    def _get_device_and_dtype(self):
        try:
            import torch  # type: ignore

            if torch.cuda.is_available():
                return "cuda", torch.float16
            return "cpu", torch.float32
        except Exception:
            return "cpu", None

    def _ensure_txt2vid(self) -> None:
        if self._txt2vid_pipe is not None:
            return
        model_id = self._settings.txt2vid_model_id
        if not model_id:
            return
        try:
            from diffusers import DiffusionPipeline  # type: ignore

            device, dtype = self._get_device_and_dtype()
            kwargs = {}
            if dtype is not None:
                kwargs["torch_dtype"] = dtype
            pipe = DiffusionPipeline.from_pretrained(model_id, **kwargs)
            if hasattr(pipe, "to") and device:
                pipe = pipe.to(device)
            self._txt2vid_pipe = pipe
        except Exception:
            self._txt2vid_pipe = None

    def _ensure_img2vid(self) -> None:
        if self._img2vid_pipe is not None:
            return
        model_id = self._settings.img2vid_model_id
        if not model_id:
            return
        try:
            from diffusers import DiffusionPipeline  # type: ignore

            device, dtype = self._get_device_and_dtype()
            kwargs = {}
            if dtype is not None:
                kwargs["torch_dtype"] = dtype
            pipe = DiffusionPipeline.from_pretrained(model_id, **kwargs)
            if hasattr(pipe, "to") and device:
                pipe = pipe.to(device)
            self._img2vid_pipe = pipe
        except Exception:
            self._img2vid_pipe = None

    def txt2vid(self, request: Txt2VidRequest) -> Tuple[Optional[str], Optional[str], str]:
        self._ensure_txt2vid()
        if self._txt2vid_pipe is None:
            return None, "model not configured", "mp4"
        try:
            result = self._txt2vid_pipe(
                request.prompt,
                negative_prompt=request.negative_prompt,
                num_frames=request.num_frames,
                num_inference_steps=request.num_inference_steps,
            )
            frames = getattr(result, "frames", None) or getattr(result, "images", None)
            if not frames:
                return None, "generation failed", "mp4"
            import imageio.v2 as imageio  # type: ignore
            from tempfile import NamedTemporaryFile

            with NamedTemporaryFile(suffix=".gif", delete=False) as tmp:
                file_path = Path(tmp.name)
            imageio.mimsave(str(file_path), frames, format="GIF", duration=0.2)
            video_base64 = encode_file_to_base64(file_path)
            file_path.unlink(missing_ok=True)
            return video_base64, None, "gif"
        except Exception:
            return None, "generation failed", "mp4"

    def img2vid(self, image_path: Path, request: Txt2VidRequest) -> Tuple[Optional[str], Optional[str], str]:
        self._ensure_img2vid()
        if self._img2vid_pipe is None:
            return None, "model not configured", "mp4"
        try:
            from PIL import Image  # type: ignore

            init_image = Image.open(image_path).convert("RGB")
            result = self._img2vid_pipe(
                prompt=request.prompt,
                image=init_image,
                negative_prompt=request.negative_prompt,
            )
            frames = getattr(result, "frames", None) or getattr(result, "images", None)
            if not frames:
                return None, "generation failed", "mp4"
            import imageio.v2 as imageio  # type: ignore
            from tempfile import NamedTemporaryFile

            with NamedTemporaryFile(suffix=".gif", delete=False) as tmp:
                file_path = Path(tmp.name)
            imageio.mimsave(str(file_path), frames, format="GIF", duration=0.2)
            video_base64 = encode_file_to_base64(file_path)
            file_path.unlink(missing_ok=True)
            return video_base64, None, "gif"
        except Exception:
            return None, "generation failed", "mp4"


@lru_cache()
def get_video_service(settings: Optional[Settings] = None) -> VideoService:
    return VideoService(settings or get_settings())
