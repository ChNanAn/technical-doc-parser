from __future__ import annotations

import json
from collections.abc import Awaitable, Callable
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
    last_event_sequence BIGINT NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
ALTER TABLE runs ADD COLUMN IF NOT EXISTS stage TEXT;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS error TEXT;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS last_event_sequence BIGINT NOT NULL DEFAULT 0;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW();
ALTER TABLE runs ADD COLUMN IF NOT EXISTS execution_id TEXT;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS cancel_requested_at TIMESTAMPTZ;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS cancel_delivered_at TIMESTAMPTZ;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS cancel_cleaned BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS artifacts_expired_at TIMESTAMPTZ;
ALTER TABLE runs ADD COLUMN IF NOT EXISTS artifacts_cleaned_at TIMESTAMPTZ;
CREATE INDEX IF NOT EXISTS runs_artifact_retention_idx ON runs(id)
    WHERE status IN ('succeeded', 'failed', 'cancelled') AND artifacts_cleaned_at IS NULL;
CREATE INDEX IF NOT EXISTS runs_cancel_pending_idx ON runs(cancel_requested_at)
    WHERE cancel_requested_at IS NOT NULL AND cancel_delivered_at IS NULL;
CREATE INDEX IF NOT EXISTS runs_cancel_cleanup_idx ON runs(updated_at)
    WHERE cancel_requested_at IS NOT NULL AND NOT cancel_cleaned
      AND status IN ('succeeded', 'failed', 'cancelled');
CREATE INDEX IF NOT EXISTS runs_document_id_idx ON runs(document_id, created_at DESC);

CREATE TABLE IF NOT EXISTS job_outbox (
    job_id TEXT PRIMARY KEY,
    run_id TEXT NOT NULL UNIQUE REFERENCES runs(id),
    attempt_id TEXT NOT NULL,
    job_path TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    published_at TIMESTAMPTZ,
    marker_cleaned BOOLEAN NOT NULL DEFAULT FALSE
);
CREATE INDEX IF NOT EXISTS job_outbox_pending_idx ON job_outbox(created_at)
    WHERE published_at IS NULL;
CREATE INDEX IF NOT EXISTS job_outbox_cleanup_idx ON job_outbox(published_at)
    WHERE published_at IS NOT NULL AND NOT marker_cleaned;
"""


class Database:
    def __init__(self, url: str) -> None:
        self._url = url
        self._pool: asyncpg.Pool | None = None

    async def connect(self, *, initialize_schema: bool = True) -> None:
        self._pool = await asyncpg.create_pool(self._url, min_size=1, max_size=10)
        if initialize_schema:
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
        async with self.pool.acquire() as connection, connection.transaction():
            record = await connection.fetchrow(
                """INSERT INTO runs(id, document_id, attempt_id, status, options_json, job_path)
                   VALUES($1, $2, $3, 'queued', $4::jsonb, $5) RETURNING *""",
                values["id"], values["document_id"], values["attempt_id"],
                json.dumps(values["options"]), values["job_path"],
            )
            await connection.execute(
                """INSERT INTO job_outbox(job_id, run_id, attempt_id, job_path)
                   VALUES($1, $2, $3, $4)""",
                values["job_id"], values["id"], values["attempt_id"], values["job_path"],
            )
            return record

    async def dispatch_next_job(
        self, publish: Callable[[dict[str, str]], Awaitable[str | None]],
    ) -> bool:
        # The row lock serializes dispatchers. Redis keeps an idempotency marker
        # until this transaction commits, including across connection failures.
        async with self.pool.acquire() as connection, connection.transaction():
            row = await connection.fetchrow(
                """SELECT job_id, run_id, attempt_id, job_path FROM job_outbox
                   WHERE published_at IS NULL ORDER BY created_at
                   LIMIT 1 FOR UPDATE SKIP LOCKED"""
            )
            if row is None or await publish(dict(row)) is None:
                return False
            await connection.execute(
                "UPDATE job_outbox SET published_at=NOW() WHERE job_id=$1", row["job_id"],
            )
        return True

    async def published_outbox_jobs(self) -> list[asyncpg.Record]:
        return await self.pool.fetch(
            """SELECT job_id FROM job_outbox WHERE published_at IS NOT NULL
               AND NOT marker_cleaned ORDER BY published_at LIMIT 50"""
        )

    async def mark_outbox_marker_cleaned(self, job_id: str) -> None:
        await self.pool.execute(
            """UPDATE job_outbox SET marker_cleaned=TRUE
               WHERE job_id=$1 AND published_at IS NOT NULL""", job_id,
        )

    async def get_run(self, run_id: str) -> asyncpg.Record | None:
        return await self.pool.fetchrow("SELECT * FROM runs WHERE id=$1", run_id)

    async def request_cancellation(self, run_id: str) -> asyncpg.Record | None:
        # One idempotent durable request; it does not assert that execution stopped.
        await self.pool.execute(
            """UPDATE runs SET cancel_requested_at=NOW(), updated_at=NOW()
               WHERE id=$1 AND cancel_requested_at IS NULL
                 AND status NOT IN ('succeeded', 'failed', 'cancelled')""", run_id,
        )
        return await self.get_run(run_id)

    async def dispatch_next_cancellation(
        self, publish: Callable[[str, str], Awaitable[None]], run_id: str | None = None,
    ) -> bool:
        async with self.pool.acquire() as connection, connection.transaction():
            row = await connection.fetchrow(
                """SELECT id, attempt_id, status FROM runs
                   WHERE cancel_requested_at IS NOT NULL AND cancel_delivered_at IS NULL
                     AND ($1::text IS NULL OR id=$1)
                   ORDER BY cancel_requested_at LIMIT 1 FOR UPDATE SKIP LOCKED""", run_id,
            )
            if row is None:
                return False
            if row["status"] not in {"succeeded", "failed", "cancelled"}:
                await publish(row["id"], row["attempt_id"])
            await connection.execute("UPDATE runs SET cancel_delivered_at=NOW() WHERE id=$1", row["id"])
        return True

    async def cleanup_next_cancellation(self, remove: Callable[[str], Awaitable[None]]) -> bool:
        # Share the row lock with dispatch/projection so uncertain delivery cannot
        # recreate a marker after durable terminal cleanup.
        async with self.pool.acquire() as connection, connection.transaction():
            row = await connection.fetchrow(
                """SELECT id FROM runs WHERE cancel_requested_at IS NOT NULL AND NOT cancel_cleaned
                     AND status IN ('succeeded', 'failed', 'cancelled')
                   ORDER BY updated_at LIMIT 1 FOR UPDATE SKIP LOCKED""",
            )
            if row is None:
                return False
            await remove(row["id"])
            await connection.execute(
                "UPDATE runs SET cancel_cleaned=TRUE, cancel_delivered_at=COALESCE(cancel_delivered_at, NOW()) WHERE id=$1",
                row["id"],
            )
        return True

    async def list_runs(self, document_id: str, limit: int = 100, offset: int = 0) -> list[asyncpg.Record]:
        return await self.pool.fetch(
            "SELECT * FROM runs WHERE document_id=$1 ORDER BY created_at DESC, id DESC LIMIT $2 OFFSET $3",
            document_id, limit, offset,
        )

    async def update_run(
        self,
        run_id: str,
        attempt_id: str,
        sequence: int,
        status: str,
        stage: str | None,
        error: str | None,
        execution_id: str | None = None,
    ) -> bool:
        result = await self.pool.execute(
            """UPDATE runs
               SET status=$4,
                   stage=CASE WHEN $7::text IS NOT NULL AND execution_id IS DISTINCT FROM $7 THEN $5
                              ELSE COALESCE($5, stage) END,
                   error=CASE WHEN $4='cancelled' THEN NULL
                              WHEN $7::text IS NOT NULL AND execution_id IS DISTINCT FROM $7 THEN $6
                              ELSE COALESCE($6, error) END,
                   execution_id=COALESCE($7, execution_id),
                   last_event_sequence=$3, updated_at=NOW()
               WHERE id=$1 AND attempt_id=$2 AND last_event_sequence < $3
                 AND (status NOT IN ('succeeded', 'failed', 'cancelled') OR status=$4)""",
            run_id,
            attempt_id,
            sequence,
            status,
            stage,
            error,
            execution_id,
        )
        return result == "UPDATE 1"
