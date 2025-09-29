# LocalAI Server Compose Setup

This directory provides a ready-to-run [LocalAI](https://github.com/go-skynet/LocalAI) container setup with health checks, persistent model caching, and helper tooling.

## Quick start

1. Copy the example environment file and adjust the values as needed:

   ```bash
   cp .env.example .env
   ```

2. (Optional) Create the model and temporary cache directories if you want to seed them manually:

   ```bash
   mkdir -p data/models data/tmp
   ```

3. Launch the stack:

   ```bash
   docker compose up --build -d
   ```

4. Tail the service logs or run smoke tests via the provided Makefile targets (see below).

The container exposes the LocalAI API on `http://localhost:${APP_PORT}` and publishes a `/healthz` endpoint used for monitoring.

## Environment variables

The `.env.example` file documents the available configuration flags:

| Variable | Default | Purpose |
| --- | --- | --- |
| `APP_PORT` | `8000` | Host port that forwards to LocalAI's port `8000` inside the container. |
| `LOG_LEVEL` | `info` | Controls LocalAI's logging verbosity. |
| `ENABLE_CUDA` | `(empty)` | Leave blank (CPU only) or set to `1` to run the container with the NVIDIA runtime and reserve GPU devices. |
| `LLM_MODEL_ID` | `distilbert-base-uncased` | Default model ID for text generation calls. |
| `STT_MODEL_ID` | `small` | Default speech-to-text model identifier. |
| `TTS_MODEL_ID` | `tts_models/en/ljspeech/tacotron2-DDC` | Default text-to-speech model. |
| `TXT2IMG_MODEL_ID` | `runwayml/stable-diffusion-v1-5` | Default text-to-image model identifier. |
| `IMG2IMG_MODEL_ID` | `runwayml/stable-diffusion-v1-5` | Default image-to-image model identifier. |
| `TXT2VID_MODEL_ID` | `damo-vilab/text-to-video-ms-1.7b` | Default text-to-video model identifier. |
| `IMG2VID_MODEL_ID` | `damo-vilab/text-to-video-ms-1.7b` | Default image-to-video model identifier. |
| `VLM_IMG2TXT_MODEL_ID` | `nlpconnect/vit-gpt2-image-captioning` | Visual-language model for image captioning. |
| `VLM_VID2TXT_MODEL_ID` | `nlpconnect/vit-gpt2-image-captioning` | Visual-language model for video captioning. |

> **GPU acceleration:** Set `ENABLE_CUDA=1` (and ensure the NVIDIA Container Toolkit is installed) to schedule the container with GPU access and switch to the NVIDIA runtime automatically.

## Makefile targets

The provided Makefile wraps the most common operations:

| Target | Description |
| --- | --- |
| `make up` | Start the LocalAI service in detached mode. |
| `make down` | Stop and remove the containers without deleting volumes. |
| `make rebuild` | Rebuild the image (if using a local Dockerfile) and restart the stack. |
| `make logs` | Follow the LocalAI container logs. |
| `make bash` | Open an interactive bash shell inside the running container. |
| `make test-smoke` | Run HTTP smoke checks against core LocalAI endpoints. |

## Health checks and volumes

The compose file publishes a Docker health check that runs `curl -f http://localhost:8000/healthz`. Persistent model and temporary caches are mounted from:

- `./data/models` → `/models`
- `./data/tmp` → `/tmp`

These directories are safe to clear or back up depending on your caching requirements.

## Cleanup

To tear everything down, including volumes, run:

```bash
docker compose down -v
```

This removes the running containers and cached model data.
