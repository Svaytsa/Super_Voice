# Local AI Model Server

The offline file relay pipeline previously tracked in this directory now lives under
`Backend/FileRelay.server/`. Please update any local scripts to reference the new location.

## Migration Checklist
- Source code, docs, and automation moved to `Backend/FileRelay.server/`.
- Docker image/tag renamed from `local-ai-model` to `file-relay` (scripts accept `LOCAL_AI_IMAGE`
  / `LOCAL_AI_MODEL_IMAGE` as legacy overrides).
- Cron plans and integration guides relocated beneath
  `Backend/FileRelay.server/docs/` and `Backend/FileRelay.server/tests/`.
- Shell helpers moved to `Backend/FileRelay.server/scripts/` with updated defaults for the
  file relay service.

This directory is intentionally left empty to host the Local AI Model service in a future update.
