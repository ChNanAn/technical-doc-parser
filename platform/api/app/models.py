from __future__ import annotations

from datetime import datetime, timezone
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field


StageName = Literal["configure", "open", "render", "text", "layout", "table", "reading_order", "assembly", "export"]


class BackendSelection(BaseModel):
    model_config = ConfigDict(extra="forbid")

    document: str = "pdf"
    ocr: str = "auto"
    layout: str = "auto"
    table: str = "auto"
    registry_config: str = ""


class RunCreate(BaseModel):
    model_config = ConfigDict(extra="forbid")

    dpi: int = Field(default=200, ge=36, le=600)
    debug: bool = True
    backends: BackendSelection = Field(default_factory=BackendSelection)
    timeout_seconds: int = Field(default=600, ge=1, le=86400)
    maximum_pages: int = Field(default=100, ge=1, le=10000)


class DocumentResponse(BaseModel):
    document_id: str
    filename: str
    media_type: str
    size_bytes: int
    sha256: str
    created_at: datetime


class RunResponse(BaseModel):
    run_id: str
    document_id: str
    attempt_id: str
    status: str
    stage: str | None = None
    options: RunCreate
    created_at: datetime
    updated_at: str | None = None
    error: str | None = None


class CapabilitiesResponse(BaseModel):
    registered: dict[str, list[str]]
    available: dict[str, list[str]]
    workers: list[dict[str, Any]]


def utc_now() -> datetime:
    return datetime.now(timezone.utc)
