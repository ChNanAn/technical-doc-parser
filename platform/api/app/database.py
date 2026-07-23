from __future__ import annotations

import json
from typing import Any

import asyncpg


SCHEMA = """
CREATE TABLE IF NOT EXISTS documents (
    id TEXT PRIMARY KEY,
    filename TEXT NOT NULL,
    media_type TEXT NOT NULL,
    size_bytes BIGINT NOT NULL,
    sha256 TEXT NOT NULL,
    input_path TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS runs (
    id TEXT PRIMARY KEY,
    document_id TEXT NOT NULL REFERENCES documents(id),
    attempt_id TEXT NOT NULL,
    status TEXT NOT NULL,
    options_json JSONB NOT NULL,
    job_path TEXT NOT NULL,
    stage TEXT,
    error TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
ALTER TABLE runs ADD COLUMN IF NOT EXISTS stage TEXT;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS error TEXT;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW();
CREATE INDEX IF NOT EXISTS runs_document_id_idx ON runs(document_id, created_at DESC);
"""


class Database:
    def __init__(self, url: str) -> None:
        self._url = url
        self._pool: asyncpg.Pool | None = None

    async def connect(self) -> None:
        self._pool = await asyncpg.create_pool(self._url, min_size=1, max_size=10)
        async with self._pool.acquire() as connection:
            await connection.execute(SCHEMA)

    async def close(self) -> None:
        if self._pool is not None:
            await self._pool.close()

    @property
    def pool(self) -> asyncpg.Pool:
        if self._pool is None:
            raise RuntimeError("database is not connected")
        return self._pool

    async def create_document(self, values: dict[str, Any]) -> asyncpg.Record:
        return await self.pool.fetchrow(
            """INSERT INTO documents(id, filename, media_type, size_bytes, sha256, input_path)
               VALUES($1, $2, $3, $4, $5, $6) RETURNING *""",
            values["id"], values["filename"], values["media_type"], values["size_bytes"],
            values["sha256"], values["input_path"],
        )

    async def get_document(self, document_id: str) -> asyncpg.Record | None:
        return await self.pool.fetchrow("SELECT * FROM documents WHERE id=$1", document_id)

    async def create_run(self, values: dict[str, Any]) -> asyncpg.Record:
        return await self.pool.fetchrow(
            """INSERT INTO runs(id, document_id, attempt_id, status, options_json, job_path)
               VALUES($1, $2, $3, 'queued', $4::jsonb, $5) RETURNING *""",
            values["id"], values["document_id"], values["attempt_id"],
            json.dumps(values["options"]), values["job_path"],
        )

    async def get_run(self, run_id: str) -> asyncpg.Record | None:
        return await self.pool.fetchrow("SELECT * FROM runs WHERE id=$1", run_id)

    async def list_runs(self, document_id: str) -> list[asyncpg.Record]:
        return await self.pool.fetch(
            "SELECT * FROM runs WHERE document_id=$1 ORDER BY created_at DESC", document_id
        )

    async def update_run(self, run_id: str, status: str, stage: str | None, error: str | None) -> None:
        await self.pool.execute(
            "UPDATE runs SET status=$2, stage=$3, error=$4, updated_at=NOW() WHERE id=$1",
            run_id, status, stage, error,
        )
