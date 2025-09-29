from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import List, Optional

from ..core.config import Settings, get_settings
class VLMService:
    def __init__(self, settings: Settings):
        self._settings = settings
        self._image_pipeline = None
        self._video_pipeline = None

    def _get_device(self) -> int:
        try:
            import torch  # type: ignore

            return 0 if torch.cuda.is_available() else -1
        except Exception:
            return -1

    def _ensure_image_pipeline(self) -> None:
        if self._image_pipeline is not None:
            return
        try:
            from transformers import pipeline  # type: ignore

            device = self._get_device()
            self._image_pipeline = pipeline(
                "image-to-text",
                model=self._settings.vlm_img2txt_model_id,
                device=device,
            )
        except Exception:
            self._image_pipeline = None

    def _ensure_video_pipeline(self) -> None:
        if self._video_pipeline is not None:
            return
        model_id = self._settings.vlm_vid2txt_model_id or self._settings.vlm_img2txt_model_id
        if not model_id:
            return
        try:
            from transformers import pipeline  # type: ignore

            device = self._get_device()
            task = "image-to-text" if self._settings.vlm_vid2txt_model_id is None else "video-classification"
            self._video_pipeline = pipeline(task, model=model_id, device=device)
        except Exception:
            self._video_pipeline = None

    def image_to_text(self, image_path: Path) -> str:
        self._ensure_image_pipeline()
        if self._image_pipeline is None:
            return ""
        try:
            from PIL import Image  # type: ignore

            image = Image.open(image_path).convert("RGB")
            outputs = self._image_pipeline(image)
            if outputs and isinstance(outputs, list):
                caption = outputs[0].get("generated_text") or outputs[0].get("caption", "")
                return str(caption)
            return ""
        except Exception:
            return ""

    def video_to_text(self, video_path: Path, max_frames: int = 8) -> str:
        self._ensure_video_pipeline()
        pipeline = self._video_pipeline
        if pipeline is None:
            self._ensure_image_pipeline()
            pipeline = self._image_pipeline
        if pipeline is None:
            return ""
        try:
            import cv2  # type: ignore
            from PIL import Image  # type: ignore

            cap = cv2.VideoCapture(str(video_path))
            frames: List[Image.Image] = []
            frame_idx = 0
            stride = 5
            while len(frames) < max_frames:
                ret, frame = cap.read()
                if not ret:
                    break
                if frame_idx % stride == 0:
                    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                    frames.append(Image.fromarray(rgb))
                frame_idx += 1
            cap.release()
            if not frames:
                return ""
            captions = []
            for image in frames:
                outputs = pipeline(image)
                if outputs and isinstance(outputs, list):
                    captions.append(
                        outputs[0].get("generated_text")
                        or outputs[0].get("caption")
                        or outputs[0].get("label")
                        or ""
                    )
            return " ".join(caption for caption in captions if caption)
        except Exception:
            return ""


@lru_cache()
def get_vlm_service(settings: Optional[Settings] = None) -> VLMService:
    return VLMService(settings or get_settings())
