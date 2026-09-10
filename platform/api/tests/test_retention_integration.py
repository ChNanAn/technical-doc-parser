"""Retention against disposable DB/Redis, actual downloads and a paused Worker."""
from __future__ import annotations

import asyncio
import json
import os
import signal
import subprocess
import sys
from pathlib import Path

import httpx
import pytest
from starlette.requests import Request

from app import retention
from app.artifact_storage import ArtifactLock
from app.retention import cleanup_run_artifacts, retention_batch
from app.settings import Settings
from test_delivery_integration import DATABASE_URL, REDIS_URL, WORKER, cleanup_run, create_run, dispatch_once, services
from test_worker_recovery_integration import first_image, input_fixture, project_run, terminal_event, wait_for, worker_process


pytestmark = pytest.mark.skipif(not DATABASE_URL or not REDIS_URL, reason="disposable services required")


async def make_old(database, record, status="succeeded"):
    await database.update_run(record["id"], record["attempt_id"], 1000, status, None, None)
    await database.pool.execute("UPDATE runs SET updated_at=NOW() - INTERVAL '2 days' WHERE id=$1", record["id"])


def output_files(record):
    root = Path(record["job_path"]).parent
    for relative in ["output/document.json", "artifacts/doc.json", "executions/execution_1/output/page.png",
                     "executions/execution_2/output/page.png", "events.ndjson"]:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("{}")
    return root


@pytest.mark.parametrize("status", ["succeeded", "failed", "cancelled"])
def test_preview_and_cleanup_preserve_input_job_and_run_history(tmp_path, status):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path)
            app, record = await create_run(database, redis, token, tmp_path)
            root = output_files(record)
            source = (tmp_path / "input.pdf").read_bytes()
            job = (root / "job.json").read_bytes()
            try:
                await make_old(database, record, status)
                settings = Settings(runtime_root=tmp_path, artifact_retention_seconds=86400)
                preview, cursor = await retention_batch(database, settings)
                assert preview[0]["action"] == "preview" and not cursor
                assert set(preview[0]["entries"]) == {"output", "artifacts", "executions", "events.ndjson"}
                assert not (root / ".artifacts.lock").exists()
                assert (await database.get_run(record["id"]))["artifacts_expired_at"] is None
                results, _ = await retention_batch(database, settings, apply=True)
                assert results[0]["action"] == "cleaned"
                assert (tmp_path / "input.pdf").read_bytes() == source
                assert (root / "job.json").read_bytes() == job
                assert not (root / "executions").exists()
                assert (await database.get_run(record["id"]))["artifacts_cleaned_at"] is not None
                assert await retention_batch(database, settings, apply=True) == ([], "")
                async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                    response = await client.get(f"/api/v1/runs/{record['id']}")
                    assert response.json()["status"] == status
                    assert response.json()["artifacts_expired_at"]
                    for suffix in ["artifacts", "artifacts/doc", "stages/layout"]:
                        assert (await client.get(f"/api/v1/runs/{record['id']}/{suffix}")).status_code == 410
                    assert (await client.get(f"/api/v1/documents/{record['document_id']}/runs")).json()[0]["artifacts_expired_at"]
                    # The retained source remains reusable after all old executions expire.
                    assert (await client.post(f"/api/v1/documents/{record['document_id']}/runs", json={})).status_code == 202
                print(status, "-> preview without writes -> generated files removed -> input/job/history retained -> HTTP 410")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


@pytest.mark.parametrize("status", ["queued", "running", "recent-terminal"])
def test_retention_requires_durable_old_terminal_state(tmp_path, status):
    async def check():
        async with services() as (database, redis, token):
            _, record = await create_run(database, redis, token, tmp_path)
            root = output_files(record)
            try:
                if status == "recent-terminal":
                    await database.update_run(record["id"], record["attempt_id"], 1, "succeeded", None, None)
                else:
                    await database.request_cancellation(record["id"])
                    await database.pool.execute("UPDATE runs SET status=$2, updated_at=NOW()-INTERVAL '2 days' WHERE id=$1",
                                                record["id"], status)
                    await redis.hset(f"run:{record['id']}", mapping={"status": "succeeded"})
                settings = Settings(runtime_root=tmp_path, artifact_retention_seconds=86400)
                assert await retention_batch(database, settings, apply=True) == ([], "")
                assert (root / "output/document.json").exists()
                await make_old(database, record)
                assert await retention_batch(database, Settings(runtime_root=tmp_path), apply=True) == ([], "")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_partial_deletion_is_unavailable_and_retried_after_failure(tmp_path, monkeypatch):
    async def check():
        async with services() as (database, redis, token):
            app, record = await create_run(database, redis, token, tmp_path)
            root = output_files(record)
            try:
                await make_old(database, record)
                original = retention.remove_generated_entries
                def interrupted(lock):
                    (root / "output/document.json").unlink()
                    raise OSError("injected interrupted deletion")
                monkeypatch.setattr(retention, "remove_generated_entries", interrupted)
                settings = Settings(runtime_root=tmp_path, artifact_retention_seconds=86400)
                results, _ = await retention_batch(database, settings, apply=True)
                assert results[0]["action"] == "error"
                persisted = await database.get_run(record["id"])
                assert persisted["artifacts_expired_at"] and persisted["artifacts_cleaned_at"] is None
                async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                    assert (await client.get(f"/api/v1/runs/{record['id']}/artifacts")).status_code == 410
                monkeypatch.setattr(retention, "remove_generated_entries", original)
                # Even a longer retention period cannot forget a previously started cleanup.
                settings.artifact_retention_seconds = 86400 * 30
                results, _ = await retention_batch(database, settings, apply=True)
                assert results[0]["action"] == "cleaned"
                assert not (root / "executions").exists()
                print("partial deletion -> durable expired state -> HTTP 410 -> retry completes")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_busy_run_does_not_block_later_batches_or_concurrent_cleaners(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            _, first = await create_run(database, redis, token, tmp_path)
            _, second = await create_run(database, redis, token + "-second", tmp_path)
            first, second = sorted([first, second], key=lambda row: row["id"])
            try:
                for record in [first, second]:
                    output_files(record)
                    await make_old(database, record)
                settings = Settings(runtime_root=tmp_path, artifact_retention_seconds=86400, artifact_cleanup_batch_size=1)
                with ArtifactLock(tmp_path, first["id"]):
                    results, cursor = await retention_batch(database, settings, apply=True)
                    assert results[0]["action"] == "busy" and cursor == first["id"]
                    results, _ = await retention_batch(database, settings, apply=True, after=cursor)
                    assert results[0]["run_id"] == second["id"] and results[0]["action"] == "cleaned"
                    assert (await database.get_run(first["id"]))["artifacts_expired_at"] is None
                results = await asyncio.gather(*(cleanup_run_artifacts(database, tmp_path, first["id"], 86400)
                                                for _ in range(2)))
                assert results.count("cleaned") == 1
                assert (await database.get_run(first["id"]))["artifacts_cleaned_at"]
            finally:
                await cleanup_run(redis, first)
                await cleanup_run(redis, second)
    asyncio.run(check())


async def download_response(app, record, root):
    artifact = root / "output/document.json"
    artifact.write_bytes(b"x" * (1024 * 256))
    (root / "artifacts/doc.json").write_text(json.dumps({"uri": artifact.as_uri(), "media_type": "application/json"}))
    route = next(route for route in app.routes if route.path == "/api/v1/runs/{run_id}/artifacts/{artifact_id}")
    scope = {"type": "http", "method": "GET", "path": "/", "headers": [], "app": app}
    response = await route.endpoint(record["id"], "doc", Request(scope))
    return response, scope, artifact.read_bytes()


@pytest.mark.parametrize("cleanup_first", [False, True])
def test_download_lock_covers_response_body_and_validation_to_send_gap(tmp_path, cleanup_first):
    async def check():
        async with services() as (database, redis, token):
            app, record = await create_run(database, redis, token, tmp_path)
            root = output_files(record)
            try:
                await make_old(database, record)
                response, scope, original = await download_response(app, record, root)
                started, release = asyncio.Event(), asyncio.Event()
                messages = []
                async def send(message):
                    messages.append(message)
                    if message["type"] == "http.response.body" and not cleanup_first:
                        started.set()
                        await release.wait()
                async def receive():
                    return {"type": "http.disconnect"}
                if cleanup_first:
                    assert await cleanup_run_artifacts(database, tmp_path, record["id"], 86400) == "cleaned"
                    await response(scope, receive, send)
                    assert messages[0]["status"] == 410
                else:
                    downloading = asyncio.create_task(response(scope, receive, send))
                    try:
                        await asyncio.wait_for(started.wait(), 5)
                        assert await cleanup_run_artifacts(database, tmp_path, record["id"], 86400) == "busy"
                    finally:
                        release.set()
                        await downloading
                    assert b"".join(m["body"] for m in messages if m["type"] == "http.response.body") == original
                    assert await cleanup_run_artifacts(database, tmp_path, record["id"], 86400) == "cleaned"
                print("cleanup_first=", cleanup_first, "-> 410 before send or full download before cleanup")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


@pytest.mark.skipif(not WORKER, reason="real C++ Worker required")
def test_paused_superseded_worker_keeps_all_execution_files_until_exit(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path, "pdfjs-tracemonkey.pdf")
            _, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            try:
                await dispatch_once(redis, database, stream, 10)
                with worker_process(tmp_path, stream, token + "-original") as original:
                    await wait_for(lambda: first_image(redis, record))
                    original.send_signal(signal.SIGSTOP)
                    with worker_process(tmp_path, stream, token + "-replacement"):
                        assert (await wait_for(lambda: terminal_event(redis, record)))["execution_id"] == "execution_2"
                    await project_run(redis, database, record)
                    await database.pool.execute("UPDATE runs SET updated_at=NOW()-INTERVAL '2 days' WHERE id=$1", record["id"])
                    assert await cleanup_run_artifacts(database, tmp_path, record["id"], 86400) == "busy"
                    root = Path(record["job_path"]).parent
                    assert (root / "executions/execution_1/output").exists()
                    assert (root / "executions/execution_2/output/document.json").exists()
                    original.send_signal(signal.SIGCONT)
                    await asyncio.to_thread(original.wait, timeout=8)
                    assert await cleanup_run_artifacts(database, tmp_path, record["id"], 86400) == "cleaned"
                    assert not (root / "executions").exists()
                    print("paused old Worker + completed replacement -> cleanup busy; old Worker exits -> cleanup succeeds")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_expiry_commit_prevents_stale_download_when_disk_marker_write_fails(tmp_path, monkeypatch):
    async def check():
        async with services() as (database, redis, token):
            app, record = await create_run(database, redis, token, tmp_path)
            root = output_files(record)
            try:
                await make_old(database, record)
                response, scope, _ = await download_response(app, record, root)
                def disk_full(lock):
                    raise OSError("injected full disk before marker creation")
                monkeypatch.setattr(retention, "remove_generated_entries", disk_full)
                with pytest.raises(OSError, match="full disk"):
                    await cleanup_run_artifacts(database, tmp_path, record["id"], 86400)
                assert not (root / ".artifacts-expired").exists()
                messages = []
                async def send(message):
                    messages.append(message)
                await response(scope, None, send)
                assert messages[0]["status"] == 410
                print("expiry committed + marker creation fails -> stale download still returns 410")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_cleanup_retries_after_files_removed_but_completion_record_fails(tmp_path, monkeypatch):
    class FailingCompletionPool:
        def __init__(self, pool):
            self.pool = pool
        def acquire(self):
            return self.pool.acquire()
        async def execute(self, *args):
            raise OSError("injected DB failure recording cleanup completion")
    async def check():
        async with services() as (database, redis, token):
            _, record = await create_run(database, redis, token, tmp_path)
            root = output_files(record)
            try:
                await make_old(database, record)
                pool = database.pool
                with monkeypatch.context() as patch:
                    patch.setattr(database, "_pool", FailingCompletionPool(pool))
                    with pytest.raises(OSError, match="DB failure"):
                        await cleanup_run_artifacts(database, tmp_path, record["id"], 86400)
                assert not (root / "executions").exists()
                assert (await database.get_run(record["id"]))["artifacts_cleaned_at"] is None
                assert await cleanup_run_artifacts(database, tmp_path, record["id"], 86400) == "cleaned"
                print("files removed + DB completion fails -> idempotent retry completes")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_wrong_runtime_root_and_malformed_job_path_never_expire_the_run(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            _, record = await create_run(database, redis, token, tmp_path)
            root = output_files(record)
            try:
                await make_old(database, record)
                with pytest.raises(ValueError, match="canonical Job path"):
                    await cleanup_run_artifacts(database, tmp_path / "wrong", record["id"], 86400)
                await database.pool.execute("UPDATE runs SET job_path=$2 WHERE id=$1", record["id"], str(tmp_path / "job.json"))
                with pytest.raises(ValueError, match="canonical Job path"):
                    await cleanup_run_artifacts(database, tmp_path, record["id"], 86400)
                assert (await database.get_run(record["id"]))["artifacts_expired_at"] is None
                assert (root / "executions").exists()
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_cli_preview_then_opt_in_api_lifecycle_cleanup(tmp_path):
    from app.main import create_app
    async def check():
        async with services() as (database, redis, token):
            _, record = await create_run(database, redis, token, tmp_path)
            root = output_files(record)
            try:
                await make_old(database, record)
                schema = await database.pool.fetchval("SELECT current_schema()")
                url = DATABASE_URL + ("&" if "?" in DATABASE_URL else "?") + "search_path=" + schema
                env = dict(os.environ, DIE_DATABASE_URL=url, DIE_RUNTIME_ROOT=str(tmp_path),
                           DIE_ARTIFACT_RETENTION_SECONDS="86400")
                command = await asyncio.to_thread(subprocess.run, [sys.executable, "-m", "app.retention"],
                                                   env=env, capture_output=True, text=True, timeout=10)
                assert command.returncode == 0, command.stderr
                assert json.loads(command.stdout)["action"] == "preview"
                assert (root / "executions").exists() and not (root / ".artifacts.lock").exists()
                options = dict(database_url=url, redis_url=REDIS_URL, runtime_root=tmp_path,
                               job_stream="jobs:" + token, artifact_cleanup_interval_seconds=0.02)
                disabled = create_app(Settings(**options))
                async with disabled.router.lifespan_context(disabled):
                    await asyncio.sleep(0.06)
                    assert (await database.get_run(record["id"]))["artifacts_expired_at"] is None
                enabled = create_app(Settings(**options, artifact_retention_seconds=86400))
                async def cleaned():
                    return (await database.get_run(record["id"]))["artifacts_cleaned_at"]
                async with enabled.router.lifespan_context(enabled):
                    await wait_for(cleaned, seconds=5)
                assert not (root / "executions").exists()
                print("CLI preview leaves files/DB untouched; default API retains; enabled API cleans and shuts down")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())
