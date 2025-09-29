from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import Optional

from ..core.config import Settings, get_settings
from ..schemas.images import Txt2ImgRequest
from ..utils import encode_file_to_base64


class ImageService:
    def __init__(self, settings: Settings):
        self._settings = settings
        self._txt2img_pipe = None
        self._img2img_pipe = None

    def _get_device_and_dtype(self):
        try:
            import torch  # type: ignore

            if torch.cuda.is_available():
                return "cuda", torch.float16
            return "cpu", torch.float32
        except Exception:
            return "cpu", None

    def _ensure_txt2img(self) -> None:
        if self._txt2img_pipe is not None:
            return
        try:
            from diffusers import StableDiffusionPipeline  # type: ignore

            device, dtype = self._get_device_and_dtype()
            kwargs = dict(
                safety_checker=None,
                requires_safety_checker=False,
            )
            if dtype is not None:
                kwargs["torch_dtype"] = dtype
            pipe = StableDiffusionPipeline.from_pretrained(
                self._settings.txt2img_model_id,
                **kwargs,
            )
            if device:
                pipe = pipe.to(device)
            pipe.set_progress_bar_config(disable=True)
            self._txt2img_pipe = pipe
        except Exception:
            self._txt2img_pipe = None

    def _ensure_img2img(self) -> None:
        if self._img2img_pipe is not None:
            return
        try:
            from diffusers import StableDiffusionImg2ImgPipeline  # type: ignore

            device, dtype = self._get_device_and_dtype()
            kwargs = dict(
                safety_checker=None,
                requires_safety_checker=False,
            )
            if dtype is not None:
                kwargs["torch_dtype"] = dtype
            pipe = StableDiffusionImg2ImgPipeline.from_pretrained(
                self._settings.img2img_model_id,
                **kwargs,
            )
            if device:
                pipe = pipe.to(device)
            pipe.set_progress_bar_config(disable=True)
            self._img2img_pipe = pipe
        except Exception:
            self._img2img_pipe = None

    def txt2img(self, request: Txt2ImgRequest) -> Optional[str]:
        self._ensure_txt2img()
        if self._txt2img_pipe is None:
            return None
        try:
            generator = None
            if request.seed is not None:
                import torch  # type: ignore

                generator = torch.Generator(device=self._txt2img_pipe.device).manual_seed(
                    request.seed
                )
            result = self._txt2img_pipe(
                prompt=request.prompt,
                negative_prompt=request.negative_prompt,
                num_inference_steps=request.num_inference_steps,
                guidance_scale=request.guidance_scale,
                height=request.height,
                width=request.width,
                generator=generator,
            )
            image = result.images[0]
            from tempfile import NamedTemporaryFile

            with NamedTemporaryFile(suffix=".png", delete=False) as tmp:
                file_path = Path(tmp.name)
            image.save(str(file_path), format="PNG")
            image_base64 = encode_file_to_base64(file_path)
            file_path.unlink(missing_ok=True)
            return image_base64
        except Exception:
            return None

    def img2img(self, image_path: Path, prompt: str | None, strength: float) -> Optional[str]:
        self._ensure_img2img()
        if self._img2img_pipe is None:
            return None
        try:
            from PIL import Image  # type: ignore

            init_image = Image.open(image_path).convert("RGB")
            result = self._img2img_pipe(
                prompt=prompt,
                image=init_image,
                strength=strength,
                guidance_scale=7.5,
            )
            image = result.images[0]
            from tempfile import NamedTemporaryFile

            with NamedTemporaryFile(suffix=".png", delete=False) as tmp:
                file_path = Path(tmp.name)
            image.save(str(file_path), format="PNG")
            image_base64 = encode_file_to_base64(file_path)
            file_path.unlink(missing_ok=True)
            return image_base64
        except Exception:
            return None


@lru_cache()
def get_image_service(settings: Optional[Settings] = None) -> ImageService:
    return ImageService(settings or get_settings())
