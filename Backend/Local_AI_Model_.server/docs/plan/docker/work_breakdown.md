# Docker Work Breakdown

## Overview
- Create a single `Dockerfile` that supports both build stages and runtime stages using a multi-stage setup.
- Introduce a build argument `ARG ROLE` that accepts `client` or `server` to switch stage-specific dependencies and copy steps.
- Configure the runtime stage to set `ENTRYPOINT` indirectly using an `ENV` variable so scripts can override it.

## Dockerfile Tasks
1. **Base build stage**
   - Install shared dependencies (system packages, Python runtime, common tooling).
   - Copy reusable build assets (common source, dependency manifests).
2. **Client build stage**
   - Extend the base stage when `ROLE=client`.
   - Install client-only dependencies and build artifacts.
   - Publish built client assets into an export-friendly location for the runtime stage.
3. **Server build stage**
   - Extend the base stage when `ROLE=server`.
   - Install server-side dependencies and compile any native extensions.
   - Copy patches/files required only for the server runtime.
4. **Runtime stage**
   - Receive artifacts from either the client or server build stage depending on `ROLE`.
   - Expose configuration via environment variables including `ENTRYPOINT` target set by `ENV APP_START`.
   - Default to `ENTRYPOINT ["/bin/sh", "-c", "$APP_START"]`, allowing role-specific scripts to define `APP_START`.

## Support Scripts
- Provide launch helpers in `scripts/run_client.sh` and `scripts/run_server.sh` for Unix.
- Provide Windows equivalents `scripts/run_client.bat` and `scripts/run_server.bat`.
- Each script should set the appropriate `ROLE`, map host directories, inject `APP_START`, and proxy additional CLI arguments to `docker run`.
- Scripts should call `docker run --rm -it local-ai:<role>` so the same image can be reused with different roles and volume mounts.

## Volume Mounting Strategy
- **Client role**: Mount a Windows-accessible host directory containing client assets into `/app/client_mount`.
- **Server role**: Mount host `patches` and `files` directories into `/app/server/patches` and `/app/server/files` for runtime customization.

## Example Commands
```bash
# Build image for client
docker build -t local-ai:client --build-arg ROLE=client .

# Run client container mounting Windows directory
./scripts/run_client.sh \
  -v /mnt/c/Users/Name/Projects/ClientAssets:/app/client_mount \
  -e APP_START="python client/main.py"

# Build image for server
docker build -t local-ai:server --build-arg ROLE=server .

# Run server container with patches and files mounted
./scripts/run_server.sh \
  -v $(pwd)/patches:/app/server/patches \
  -v $(pwd)/files:/app/server/files \
  -e APP_START="python server/main.py"

# Direct docker run example overriding APP_START manually
docker run --rm -it \
  -e ROLE=server \
  -e APP_START="python server/main.py" \
  -v $(pwd)/patches:/app/server/patches \
  -v $(pwd)/files:/app/server/files \
  local-ai:server
```

## Windows PowerShell Examples
```powershell
# Run client
./scripts/run_client.bat ^
  -v C:\\Projects\\ClientAssets:/app/client_mount ^
  -e APP_START="python client/main.py"

# Run server
./scripts/run_server.bat ^
  -v %CD%\\patches:/app/server/patches ^
  -v %CD%\\files:/app/server/files ^
  -e APP_START="python server/main.py"
```
