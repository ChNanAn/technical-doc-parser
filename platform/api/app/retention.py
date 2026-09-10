"""Opt-in retention for generated Run files; CLI defaults to read-only preview."""
from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
from pathlib import Path

from .artifact_storage import ArtifactLock, GENERATED_ENTRIES, finish_thread, open_run_directory, remove_generated_entries, run_directory
from .database import Database
from .settings import Settings


LOGGER = logging.getLogger(__name__)
ELIGIBLE = """status IN ('succeeded', 'failed', 'cancelled') AND artifacts_cleaned_at IS NULL
    AND (artifacts_expired_at IS NOT NULL OR updated_at <= NOW() - $1 * INTERVAL '1 second')"""


def validate_job_path(root: Path, record) -> Path:
    directory = run_directory(root, record["id"])
    if Path(os.path.abspath(record["job_path"])) != directory / "job.json":
        raise ValueError("canonical Job path does not match the configured Run directory")
    return directory


async def cleanup_run_artifacts(database: Database, root: Path, run_id: str, age: int) -> str:
    if age <= 0:
        raise ValueError("artifact retention age must be positive")
    lock = None
    try:
        async with database.pool.acquire() as connection, connection.transaction():
            record = await connection.fetchrow(
                f"SELECT * FROM runs WHERE {ELIGIBLE} AND id=$2 FOR UPDATE SKIP LOCKED", age, run_id,
            )
            if record is None:
                return "skipped"
            validate_job_path(root, record)
            try:
                lock = ArtifactLock(root, run_id, exclusive=True)
            except BlockingIOError:
                LOGGER.info("artifact cleanup deferred run_id=%s reason=directory_in_use", run_id)
                return "busy"
            # Commit unavailability before irreversible filesystem work. Failed/partial
            # deletion stays eligible and can be retried after a process or DB failure.
            await connection.execute(
                "UPDATE runs SET artifacts_expired_at=COALESCE(artifacts_expired_at, NOW()) WHERE id=$1", run_id,
            )
        LOGGER.info("artifact cleanup started run_id=%s directory=%s", run_id, run_directory(root, run_id))
        await finish_thread(remove_generated_entries, lock)
        await database.pool.execute("UPDATE runs SET artifacts_cleaned_at=NOW() WHERE id=$1", run_id)
        LOGGER.info("artifact cleanup completed run_id=%s", run_id)
        return "cleaned"
    finally:
        if lock is not None:
            lock.close()


async def retention_batch(database: Database, settings: Settings, *, apply: bool = False,
                          after: str = "") -> tuple[list[dict], str]:
    if settings.artifact_retention_seconds == 0:
        return [], ""
    records = await database.pool.fetch(
        f"SELECT id, job_path FROM runs WHERE {ELIGIBLE} AND id > $2 ORDER BY id LIMIT $3",
        settings.artifact_retention_seconds, after, settings.artifact_cleanup_batch_size,
    )
    results = []
    for record in records:
        result = {"run_id": record["id"]}
        try:
            directory = validate_job_path(settings.runtime_root, record)
            result["directory"] = str(directory)
            if apply:
                result["action"] = await cleanup_run_artifacts(
                    database, settings.runtime_root, record["id"], settings.artifact_retention_seconds,
                )
            else:
                # No lock-file creation, DB writes or recursive traversal during preview.
                fd = open_run_directory(settings.runtime_root, record["id"])
                try:
                    present = set(os.listdir(fd))
                finally:
                    os.close(fd)
                result.update(action="preview", entries=[name for name in GENERATED_ENTRIES if name in present])
        except Exception as error:
            LOGGER.exception("artifact cleanup failed run_id=%s; retry on next sweep", record["id"])
            result.update(action="error", error=str(error))
        results.append(result)
    # Advance past busy/broken directories so one Run cannot starve the rest.
    cursor = records[-1]["id"] if len(records) == settings.artifact_cleanup_batch_size else ""
    return results, cursor


async def cleanup_artifacts(database: Database, settings: Settings) -> None:
    cursor = ""
    while True:
        try:
            _, cursor = await retention_batch(database, settings, apply=True, after=cursor)
        except Exception:
            LOGGER.exception("artifact retention sweep failed; retrying")
            cursor = ""
        await asyncio.sleep(settings.artifact_cleanup_interval_seconds if not cursor else 0.1)


async def run_command(settings: Settings, apply: bool) -> None:
    database = Database(settings.database_url)
    # Preview must not run schema migrations. Start/upgrade the API first.
    await database.connect(initialize_schema=False)
    try:
        cursor = ""
        errors = False
        while True:
            results, cursor = await retention_batch(database, settings, apply=apply, after=cursor)
            for result in results:
                print(json.dumps(result), flush=True)
                errors |= result["action"] == "error"
            if not cursor:
                break
        if errors:
            raise SystemExit(1)
    finally:
        await database.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="permanently delete eligible generated files")
    parser.add_argument("--retention-seconds", type=int, help="minimum age since durable terminal state")
    args = parser.parse_args()
    settings = Settings(**({"artifact_retention_seconds": args.retention_seconds}
                           if args.retention_seconds is not None else {}))
    if settings.artifact_retention_seconds == 0:
        parser.error("retention is disabled; set a positive --retention-seconds or DIE_ARTIFACT_RETENTION_SECONDS")
    logging.basicConfig(level=logging.INFO)
    asyncio.run(run_command(settings, args.apply))


if __name__ == "__main__":
    main()
