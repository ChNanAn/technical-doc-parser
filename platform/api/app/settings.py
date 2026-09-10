from __future__ import annotations

from pathlib import Path

from pydantic import Field, model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="DIE_", case_sensitive=False)

    database_url: str = "postgresql://document:document@127.0.0.1:5432/document"
    redis_url: str = "redis://127.0.0.1:6379/0"
    runtime_root: Path = Path("runtime")
    job_stream: str = "document-jobs"
    job_stream_max_length: int = Field(default=10_000, ge=1)
    projector_claim_idle_milliseconds: int = Field(default=30_000, ge=1)
    projector_restart_delay_seconds: float = Field(default=1.0, gt=0)
    projector_restart_max_delay_seconds: float = Field(default=30.0, gt=0)
    projector_restart_reset_seconds: float = Field(default=60.0, gt=0)
    maximum_upload_bytes: int = 100 * 1024 * 1024
    artifact_retention_seconds: int = Field(default=0, ge=0)
    artifact_cleanup_interval_seconds: float = Field(default=3600, gt=0)
    artifact_cleanup_batch_size: int = Field(default=50, ge=1, le=500)
    cors_origins: str = "http://localhost:5173,http://localhost:8080"

    @model_validator(mode="after")
    def validate_projector_backoff(self) -> "Settings":
        if self.projector_restart_max_delay_seconds < self.projector_restart_delay_seconds:
            raise ValueError("projector restart maximum delay must be at least the initial delay")
        return self

    @property
    def allowed_origins(self) -> list[str]:
        return [value.strip() for value in self.cors_origins.split(",") if value.strip()]
