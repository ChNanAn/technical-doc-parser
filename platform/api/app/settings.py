from __future__ import annotations

from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="DIE_", case_sensitive=False)

    database_url: str = "postgresql://document:document@127.0.0.1:5432/document"
    redis_url: str = "redis://127.0.0.1:6379/0"
    runtime_root: Path = Path("runtime")
    job_stream: str = "document-jobs"
    maximum_upload_bytes: int = 100 * 1024 * 1024
    cors_origins: str = "http://localhost:5173,http://localhost:8080"

    @property
    def allowed_origins(self) -> list[str]:
        return [value.strip() for value in self.cors_origins.split(",") if value.strip()]
