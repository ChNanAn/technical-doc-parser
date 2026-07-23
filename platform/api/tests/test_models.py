import asyncio
import io

import pytest
from fastapi import HTTPException, UploadFile

from app.main import _common_worker_capabilities, _save_upload
from app.models import RunCreate


def test_backend_selection_is_independent_per_stage() -> None:
    run = RunCreate.model_validate({
        "dpi": 144,
        "backends": {
            "document": "pdf",
            "ocr": "tesseract",
            "layout": "paddle-layout",
            "table": "text",
        },
    })
    assert run.backends.ocr == "tesseract"
    assert run.backends.layout == "paddle-layout"
    assert run.backends.table == "text"


def test_worker_capabilities_are_intersected_for_shared_queue() -> None:
    available = _common_worker_capabilities([
        {"capabilities": {"document": ["auto", "pdf"], "ocr": ["auto", "paddle", "noop"], "layout": ["auto", "text"], "table": ["auto", "text"]}},
        {"capabilities": {"document": ["auto", "pdf"], "ocr": ["auto", "noop"], "layout": ["auto", "text"], "table": ["auto", "text"]}},
    ])
    assert available["ocr"] == ["auto", "noop"]


def test_upload_rejects_content_without_pdf_signature(tmp_path) -> None:
    destination = tmp_path / "input.pdf"
    upload = UploadFile(filename="fake.pdf", file=io.BytesIO(b"not a PDF"))

    with pytest.raises(HTTPException) as error:
        asyncio.run(_save_upload(upload, destination, 1024))

    assert error.value.status_code == 415
    assert not destination.exists()
