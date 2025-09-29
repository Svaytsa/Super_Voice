from __future__ import annotations

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .routers import images, llm, stt, tts, videos, vlm

app = FastAPI(title="LocalAI Server", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(llm.router)
app.include_router(tts.router)
app.include_router(stt.router)
app.include_router(images.router)
app.include_router(videos.router)
app.include_router(vlm.router)


@app.get("/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}
