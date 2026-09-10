"""Run against disposable services via DIE_TEST_DATABASE_URL / DIE_TEST_REDIS_URL.

Each test owns a temporary PostgreSQL schema and unique Run/queue identities.
Set DIE_TEST_WORKER to also exercise the actual C++ Worker process.
"""
from __future__ import annotations

import asyncio
import hashlib
import json
import os
import shutil
import subprocess
import uuid
from contextlib import asynccontextmanager
from pathlib import Path
from urllib.parse import urlparse

import asyncpg
import httpx
import pytest
from redis.asyncio import Redis
from redis.exceptions import ConnectionError as RedisConnectionError

from app.database import Database, SCHEMA
from app.dispatcher import dispatch_once, enqueue_job, marker_key
from app.main import create_app
from app.projector import _project_entry
from app.settings import Settings


DATABASE_URL = os.environ.get("DIE_TEST_DATABASE_URL")
REDIS_URL = os.environ.get("DIE_TEST_REDIS_URL")
WORKER = os.environ.get("DIE_TEST_WORKER")
pytestmark = pytest.mark.skipif(not DATABASE_URL or not REDIS_URL, reason="disposable integration services not configured")


@asynccontextmanager
async def services():
    token = uuid.uuid4().hex
    schema = "review_" + token
    admin = await asyncpg.connect(DATABASE_URL)
    await admin.execute(f'CREATE SCHEMA "{schema}"')
    database = Database(DATABASE_URL)
    redis = Redis.from_url(REDIS_URL, decode_responses=True)
    try:
        database._pool = await asyncpg.create_pool(DATABASE_URL, min_size=1, max_size=4,
                                                  server_settings={"search_path": schema})
        await database.pool.execute(SCHEMA)
        await redis.ping()
        yield database, redis, token
    finally:
        keys = [key async for key in redis.scan_iter(match=f"*{token}*")]
        if keys:
            await redis.delete(*keys)
        events = await redis.xrange("platform-events")
        owned = [event_id for event_id, fields in events if token in fields.get("event", "")]
        if owned:
            await redis.xdel("platform-events", *owned)
        await redis.aclose()
        await database.close()
        await admin.execute(f'DROP SCHEMA "{schema}" CASCADE')
        await admin.close()


async def create_run(database, redis, token, root):
    document_id = "doc_" + token
    input_path = root / "input.pdf"
    size = input_path.stat().st_size if input_path.is_file() else 10
    sha256 = hashlib.sha256(input_path.read_bytes()).hexdigest() if input_path.is_file() else "0" * 64
    await database.create_document({
        "id": document_id, "filename": "input.pdf", "media_type": "application/pdf",
        "size_bytes": size, "sha256": sha256, "input_path": str(input_path),
    })
    app = create_app(Settings(runtime_root=root))
    app.state.database = database
    app.state.redis = redis
    async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
        response = await client.post(f"/api/v1/documents/{document_id}/runs", json={
            "backends": {"ocr": "noop", "layout": "text", "table": "text"},
        })
    assert response.status_code == 202, response.text
    record = await database.get_run(response.json()["run_id"])
    # Redis test cleanup is keyed by a unique token. The API generates its own
    # identities, so track those keys explicitly in the caller as well.
    return app, record


async def cleanup_run(redis, record):
    await redis.delete(f"run:{record['id']}", f"run-events:{record['id']}", f"run-cancel:{record['id']}")
    events = await redis.xrange("platform-events")
    owned = [event_id for event_id, fields in events if record["id"] in fields.get("event", "")]
    if owned:
        await redis.xdel("platform-events", *owned)


def test_run_survives_redis_outage_and_dispatches_after_recovery(tmp_path):
    class UnavailableRedis:
        async def scan_iter(self, **kwargs):
            raise RedisConnectionError("injected Redis outage")
            yield  # async iterator interface

        async def hgetall(self, key):
            raise RedisConnectionError("injected Redis outage")

    async def check():
        async with services() as (database, redis, token):
            app, record = await create_run(database, UnavailableRedis(), token, tmp_path)
            try:
                async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                    response = await client.get(f"/api/v1/runs/{record['id']}")
                assert response.status_code == 200
                assert response.json()["status"] == "queued"
                assert await database.pool.fetchval("SELECT count(*) FROM job_outbox WHERE published_at IS NULL") == 1
                stream = "jobs:" + token
                assert await dispatch_once(redis, database, stream, 10)
                assert await redis.xlen(stream) == 1
                assert await database.pool.fetchval("SELECT count(*) FROM job_outbox WHERE published_at IS NULL") == 0
                print("redis outage -> HTTP 202 + durable queued Run -> recovery -> one delivered Job")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_unknown_publish_outcome_retries_without_duplicate_or_state_regression(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            _, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            try:
                async def lost_response(fields):
                    await enqueue_job(redis, stream, fields, 10)
                    raise RedisConnectionError("injected loss after Redis accepted the Job")
                with pytest.raises(RedisConnectionError):
                    await database.dispatch_next_job(lost_response)
                assert await database.pool.fetchval("SELECT count(*) FROM job_outbox WHERE published_at IS NULL") == 1
                await redis.hset(f"run:{record['id']}", mapping={"status": "running"})
                published = await asyncio.gather(dispatch_once(redis, database, stream, 10),
                                                 dispatch_once(redis, database, stream, 10))
                assert sum(published) == 1
                assert await redis.xlen(stream) == 1
                assert await redis.hget(f"run:{record['id']}", "status") == "running"
                assert not await dispatch_once(redis, database, stream, 10)
                job_id = await database.pool.fetchval("SELECT job_id FROM job_outbox")
                assert not await redis.exists(marker_key(job_id))
                print("lost enqueue response -> retry: queue length=1, running state preserved")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_outbox_constraint_failure_rolls_back_the_run(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            _, record = await create_run(database, redis, token, tmp_path)
            job_id = await database.pool.fetchval("SELECT job_id FROM job_outbox")
            with pytest.raises(asyncpg.UniqueViolationError):
                await database.create_run({
                    "id": "run_" + token, "job_id": job_id, "document_id": record["document_id"],
                    "attempt_id": "attempt_" + token, "options": {}, "job_path": record["job_path"],
                })
            assert await database.get_run("run_" + token) is None
            assert await database.pool.fetchval("SELECT count(*) FROM job_outbox") == 1
    asyncio.run(check())


def test_run_listing_is_paginated_and_durable_terminal_state_wins(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            app, record = await create_run(database, redis, token, tmp_path)
            try:
                await database.update_run(record["id"], record["attempt_id"], 3, "failed", "open", "invalid PDF")
                await redis.hset(f"run:{record['id']}", mapping={"status": "queued"})
                async def unexpected_read(run_id):
                    raise AssertionError("list endpoint must use the records already loaded")
                database.get_run = unexpected_read
                async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                    url = f"/api/v1/documents/{record['document_id']}/runs"
                    response = await client.get(url, params={"limit": 1})
                    assert response.status_code == 200
                    assert response.json()[0]["status"] == "failed"
                    assert response.json()[0]["error"] == "invalid PDF"
                    assert (await client.get(url, params={"offset": 1})).json() == []
                    assert (await client.get(url, params={"limit": 0})).status_code == 422
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_stage_failure_stays_recoverable_and_execution_switch_clears_old_error(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            app, record = await create_run(database, redis, token, tmp_path)
            try:
                async def project(sequence, event_type, execution, **fields):
                    event = dict(type=event_type, run_id=record["id"], attempt_id=record["attempt_id"],
                                 sequence=sequence, execution_id=execution, **fields)
                    await _project_entry(redis, database, f"{sequence}-0", {"event": json.dumps(event)})
                await project(10, "stage_failed", "execution_1", stage="layout", error={"message": "interrupted"})
                failed_stage = await database.get_run(record["id"])
                assert failed_stage["status"] == "running"
                assert failed_stage["error"] == "interrupted"
                await redis.hset(f"run:{record['id']}", mapping={
                    "status": "running", "execution_id": "execution_2", "stage": "", "error": "",
                })
                async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                    current = (await client.get(f"/api/v1/runs/{record['id']}")).json()
                    print("execution switch before projection:", current["execution_id"], current["stage"], current["error"])
                    assert current["stage"] is None and current["error"] is None
                await project(11, "job_started", "execution_2")
                replacement = await database.get_run(record["id"])
                assert replacement["status"] == "running"
                assert replacement["stage"] is None and replacement["error"] is None
                await project(10, "stage_failed", "execution_1", error={"message": "stale"})
                assert (await database.get_run(record["id"]))["execution_id"] == "execution_2"
                await project(12, "job_succeeded", "execution_2")
                await project(13, "job_started", "execution_3")
                assert (await database.get_run(record["id"]))["status"] == "succeeded"
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_full_queue_preserves_pending_jobs_in_every_consumer_group():
    async def check():
        async with services() as (_, redis, token):
            stream = "jobs:" + token
            def fields(suffix):
                return {"job_id": f"job_{token}_{suffix}", "run_id": f"run_{token}_{suffix}",
                        "attempt_id": f"attempt_{token}_{suffix}", "job_path": "/unused/job.json"}
            first = await enqueue_job(redis, stream, fields("1"), 1)
            await redis.xgroup_create(stream, "workers", id="0")
            await redis.xgroup_create(stream, "other-workers", id="0")
            await redis.xreadgroup("workers", "worker", {stream: ">"})
            await redis.xreadgroup("other-workers", "worker", {stream: ">"})
            assert await enqueue_job(redis, stream, fields("2"), 1) is None
            await redis.xack(stream, "workers", first)
            assert await enqueue_job(redis, stream, fields("2"), 1) is None
            assert (await redis.xrange(stream))[0][0] == first
            await redis.xack(stream, "other-workers", first)
            assert await enqueue_job(redis, stream, fields("2"), 1) is not None
            assert await redis.xlen(stream) == 1
    asyncio.run(check())


@pytest.mark.skipif(not WORKER, reason="C++ Worker executable not configured")
@pytest.mark.parametrize("case", ["valid_document", "missing_input", "missing_job", "malformed_job"])
def test_worker_terminal_events_are_projected_and_jobs_acknowledged(tmp_path, case):
    async def check():
        async with services() as (database, redis, token):
            if case == "valid_document":
                fixture = Path(__file__).resolve().parents[3] / "tests/fixtures/pdfs/pdfjs-basicapi.pdf"
                shutil.copyfile(fixture, tmp_path / "input.pdf")
            _, record = await create_run(database, redis, token, tmp_path)
            path = Path(record["job_path"])
            if case == "missing_job":
                path.unlink()
            elif case == "malformed_job":
                path.write_text("{incomplete", encoding="utf-8")
            expected_type = "job_succeeded" if case == "valid_document" else "job_failed"
            stream = "jobs:" + token
            await dispatch_once(redis, database, stream, 10)
            address = urlparse(REDIS_URL)
            env = dict(os.environ, REDIS_HOST=address.hostname, REDIS_PORT=str(address.port or 6379),
                       JOB_STREAM=stream, WORKER_ID=token, WORKER_RUNTIME_ROOT=str(tmp_path))
            for name in ["PADDLEOCR_MODEL_DIR", "DOCLAYNET_MODEL", "PADDLE_LAYOUT_MODEL",
                         "TABLE_DETECTION_MODEL", "TABLE_STRUCTURE_MODEL"]:
                env["DOCUMENT_INTELLIGENCE_ENGINE_" + name] = str(tmp_path / "no-model")
            with (tmp_path / "worker.log").open("w") as log:
                worker = subprocess.Popen([WORKER], env=env, stdout=log, stderr=log)
                try:
                    deadline = asyncio.get_running_loop().time() + 30
                    while asyncio.get_running_loop().time() < deadline:
                        events = await redis.xrange(f"run-events:{record['id']}")
                        if events and json.loads(events[-1][1]["event"])["type"] == expected_type:
                            break
                        assert worker.poll() is None, (tmp_path / "worker.log").read_text()
                        await asyncio.sleep(0.05)
                    else:
                        pytest.fail((tmp_path / "worker.log").read_text())
                    for event_id, fields in await redis.xrange("platform-events"):
                        if record["id"] in fields.get("event", ""):
                            await _project_entry(redis, database, event_id, fields)
                    for _ in range(100):
                        if (await redis.xpending(stream, "document-workers"))["pending"] == 0:
                            break
                        await asyncio.sleep(0.02)
                    assert (await redis.xpending(stream, "document-workers"))["pending"] == 0
                    await redis.delete(f"run:{record['id']}")
                    persisted = await database.get_run(record["id"])
                    if case == "valid_document":
                        assert persisted["status"] == "succeeded"
                        execution = path.parent / "executions" / persisted["execution_id"]
                        document = json.loads((execution / "output/document.json").read_text())
                        assert len(document["pages"]) == 3
                        assert document["blocks"]
                        for kind in ["document_json", "document_markdown", "document_html"]:
                            manifest = json.loads((execution / "artifacts" / f"artifact_export_{kind}.json").read_text())
                            assert manifest["size_bytes"] > 0
                            assert Path(manifest["uri"].removeprefix("file://")).is_file()
                    else:
                        assert persisted["status"] == "failed"
                        assert persisted["error"]
                    print(f"{case}: {expected_type} event + durable terminal state + ACK verified")
                finally:
                    worker.terminate()
                    await asyncio.to_thread(worker.wait, timeout=10)
                    await cleanup_run(redis, record)
    asyncio.run(check())
