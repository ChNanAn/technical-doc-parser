#!/usr/bin/env python3
"""Exercise verified model synchronization without external network access."""

from __future__ import annotations

import hashlib
import importlib.util
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent.parent
MODULE_PATH = ROOT_DIR / "scripts" / "package_model_pack.py"
SPEC = importlib.util.spec_from_file_location("package_model_pack", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
PACKAGE_MODEL_PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE_MODEL_PACK)


class ModelHandler(BaseHTTPRequestHandler):
    payload = b"verified model fixture"
    request_count = 0

    def do_GET(self) -> None:
        type(self).request_count += 1
        self.send_response(200)
        self.send_header("Content-Length", str(len(type(self).payload)))
        self.end_headers()
        self.wfile.write(type(self).payload)

    def log_message(self, format: str, *args: object) -> None:
        pass


def manifest(url: str, payload: bytes) -> dict[str, object]:
    return {
        "files": [
            {
                "id": "fixture-model",
                "path": "fixture/model.bin",
                "sha256": hashlib.sha256(payload).hexdigest(),
                "source": {"url": url},
            }
        ]
    }


def main() -> int:
    server = ThreadingHTTPServer(("127.0.0.1", 0), ModelHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    model_url = f"http://127.0.0.1:{server.server_port}/model.bin"

    try:
        with tempfile.TemporaryDirectory() as temporary:
            models_dir = Path(temporary)
            model_path = models_dir / "fixture" / "model.bin"
            expected_manifest = manifest(model_url, ModelHandler.payload)

            PACKAGE_MODEL_PACK.sync_files(expected_manifest, models_dir)
            assert model_path.read_bytes() == ModelHandler.payload
            assert ModelHandler.request_count == 1

            PACKAGE_MODEL_PACK.sync_files(expected_manifest, models_dir)
            assert ModelHandler.request_count == 1

            model_path.write_bytes(b"damaged")
            PACKAGE_MODEL_PACK.sync_files(expected_manifest, models_dir)
            assert model_path.read_bytes() == ModelHandler.payload
            assert ModelHandler.request_count == 2

            model_path.write_bytes(b"still damaged")
            ModelHandler.payload = b"unexpected response"
            try:
                PACKAGE_MODEL_PACK.sync_files(
                    expected_manifest,
                    models_dir,
                    download_attempts=1,
                )
            except PACKAGE_MODEL_PACK.ManifestError:
                pass
            else:
                raise AssertionError("invalid downloaded content was accepted")
            assert model_path.read_bytes() == b"still damaged"
            assert not list(models_dir.rglob("*.tmp.*"))
    finally:
        server.shutdown()
        server.server_close()
        thread.join()

    print("Verified model sync download, cache, repair, and failure behavior")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
