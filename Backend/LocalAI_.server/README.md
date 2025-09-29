# LocalAI Server Container

This directory contains the Docker configuration for the LocalAI server. The image bundles all
system and Python dependencies required to run the FastAPI application with model caches stored
under `/models`.

## Building the image

CPU builds are enabled by default using the slim Python base image:

```bash
docker build -t localai-server Backend/LocalAI_.server
```

To build a CUDA-enabled image (Python is installed on top of the NVIDIA CUDA runtime), pass the
`ENABLE_CUDA` build argument:

```bash
docker build -t localai-server --build-arg ENABLE_CUDA=1 Backend/LocalAI_.server
```

## Running the container

Expose the application port (default `8000`) and mount a volume for model caches if desired:

```bash
docker run --rm -p 8000:8000 -e APP_PORT=8000 localai-server
```

You can override the serving port during runtime:

```bash
docker run --rm -p 9000:9000 -e APP_PORT=9000 localai-server
```

## Health check

Once the container is running, verify the service responds on `/healthz`:

```bash
curl -s localhost:8000/healthz
```

All Hugging Face, Transformers, and Torch caches are stored in `/models`, which is declared as a
volume so the data persists across runs when mounted.
