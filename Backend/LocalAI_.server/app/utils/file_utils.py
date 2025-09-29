from __future__ import annotations

import base64
import os
import tempfile
from pathlib import Path
def save_upload_to_temp(upload_file, suffix: str | None = None) -> Path:
    suffix = suffix or Path(upload_file.filename or "").suffix
    with tempfile.NamedTemporaryFile(delete=False, suffix=suffix) as tmp:
        upload_file.file.seek(0)
        while True:
            chunk = upload_file.file.read(1024 * 1024)
            if not chunk:
                break
            tmp.write(chunk)
        temp_path = Path(tmp.name)
    upload_file.file.seek(0)
    return temp_path


def write_bytes_to_temp(data: bytes, suffix: str = "") -> Path:
    fd, path = tempfile.mkstemp(suffix=suffix)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
    return Path(path)


def encode_file_to_base64(path: Path) -> str:
    with path.open("rb") as f:
        return base64.b64encode(f.read()).decode("utf-8")


def encode_bytes_to_base64(data: bytes) -> str:
    return base64.b64encode(data).decode("utf-8")
