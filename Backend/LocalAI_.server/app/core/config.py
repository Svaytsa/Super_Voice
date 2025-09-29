from functools import lru_cache
from pydantic import BaseSettings, Field


class Settings(BaseSettings):
    llm_model_id: str | None = Field(default=None, alias="LLM_MODEL_ID")
    stt_model_id: str = Field(default="medium", alias="STT_MODEL_ID")
    tts_model_id: str = Field(
        default="tts_models/en/ljspeech/tacotron2-DDC", alias="TTS_MODEL_ID"
    )
    txt2img_model_id: str = Field(
        default="runwayml/stable-diffusion-v1-5", alias="TXT2IMG_MODEL_ID"
    )
    img2img_model_id: str = Field(
        default="runwayml/stable-diffusion-v1-5", alias="IMG2IMG_MODEL_ID"
    )
    txt2vid_model_id: str | None = Field(default=None, alias="TXT2VID_MODEL_ID")
    img2vid_model_id: str | None = Field(default=None, alias="IMG2VID_MODEL_ID")
    vlm_img2txt_model_id: str = Field(
        default="nlpconnect/vit-gpt2-image-captioning", alias="VLM_IMG2TXT_MODEL_ID"
    )
    vlm_vid2txt_model_id: str | None = Field(
        default=None, alias="VLM_VID2TXT_MODEL_ID"
    )

    class Config:
        env_file = ".env"
        case_sensitive = False
        populate_by_name = True


@lru_cache()
def get_settings() -> Settings:
    return Settings()
