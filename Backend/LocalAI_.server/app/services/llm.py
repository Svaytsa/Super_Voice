from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import Optional

from ..core.config import Settings, get_settings
from ..schemas.llm import LLMGenerateRequest


class LLMService:
    def __init__(self, settings: Settings):
        self._settings = settings
        self._llama = None
        self._pipeline = None

    def _ensure_model(self) -> None:
        if self._llama or self._pipeline:
            return
        model_id = self._settings.llm_model_id
        if not model_id:
            return
        try:
            if model_id.endswith(".gguf") or Path(model_id).suffix == ".gguf":
                from llama_cpp import Llama  # type: ignore

                self._llama = Llama(model_path=model_id)
            else:
                from transformers import pipeline  # type: ignore

                device = -1
                try:
                    import torch  # type: ignore

                    device = 0 if torch.cuda.is_available() else -1
                except Exception:
                    device = -1
                self._pipeline = pipeline(
                    "text-generation",
                    model=model_id,
                    device=device,
                )
        except Exception:
            self._llama = None
            self._pipeline = None

    def generate(self, request: LLMGenerateRequest) -> str:
        model_id = self._settings.llm_model_id
        if not model_id:
            return request.prompt
        self._ensure_model()
        temperature = request.temperature or 0.7
        top_p = request.top_p or 0.95
        max_new_tokens = request.max_new_tokens or 128
        if self._llama:
            output = self._llama(
                request.prompt,
                max_tokens=max_new_tokens,
                temperature=temperature,
                top_p=top_p,
            )
            try:
                return output["choices"][0]["text"]
            except Exception:
                return str(output)
        if self._pipeline:
            result = self._pipeline(
                request.prompt,
                max_new_tokens=max_new_tokens,
                temperature=temperature,
                top_p=top_p,
            )
            try:
                return result[0]["generated_text"]
            except Exception:
                return str(result)
        return request.prompt


@lru_cache()
def get_llm_service(settings: Optional[Settings] = None) -> LLMService:
    return LLMService(settings or get_settings())
