# syntax=docker/dockerfile:1.4
ARG ENABLE_CUDA=0
ARG APP_PORT=8000

FROM python:3.10-slim AS cpu-base
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ffmpeg \
        git \
        libgl1 \
        libsndfile1 \
        libportaudio2 \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
RUN python -m venv /opt/venv \
    && /opt/venv/bin/pip install --upgrade pip setuptools wheel
ENV PATH="/opt/venv/bin:$PATH"

FROM nvidia/cuda:12.1.1-cudnn8-runtime-ubuntu22.04 AS cuda-base
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        python3 \
        python3-pip \
        python3-venv \
        ffmpeg \
        git \
        libgl1 \
        libsndfile1 \
        libportaudio2 \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --upgrade pip setuptools wheel \
    && ln -sf /opt/venv/bin/python /usr/bin/python \
    && ln -sf /opt/venv/bin/pip /usr/bin/pip
ENV PATH="/opt/venv/bin:$PATH"

FROM cpu-base AS runtime
ARG ENABLE_CUDA=0
ARG APP_PORT=8000
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN --mount=type=bind,from=cuda-base,src=/,dst=/tmp/cuda-root \
    if [[ "${ENABLE_CUDA}" == "1" ]]; then \
        cd /tmp/cuda-root \
        && tar cf - . | tar xpf - -C /; \
    fi
ENV PATH="/opt/venv/bin:$PATH"
ENV HF_HOME=/models/hf \
    TRANSFORMERS_CACHE=/models/hf \
    TORCH_HOME=/models/torch \
    XDG_CACHE_HOME=/models/.cache \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    APP_PORT=${APP_PORT}
RUN mkdir -p /models/hf /models/torch /models/.cache
WORKDIR /app
VOLUME ["/models"]

COPY requirements.txt ./
RUN if [[ "${ENABLE_CUDA}" == "1" ]]; then \
        pip install --no-cache-dir -r requirements.txt; \
    else \
        pip install --no-cache-dir --extra-index-url https://download.pytorch.org/whl/cpu -r requirements.txt; \
    fi

COPY . .

EXPOSE ${APP_PORT}
CMD ["/bin/bash", "-c", "uvicorn app:app --host 0.0.0.0 --port ${APP_PORT}"]
