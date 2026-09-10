"""Durable cancellation and Worker publication races, using disposable services."""
from __future__ import annotations

import asyncio
import json
import signal
from contextlib import nullcontext
from pathlib import Path
from urllib.parse import urlparse

import httpx
import pytest
from redis.exceptions import ConnectionError as RedisConnectionError

from app.cancellation import cancellation_key, cleanup_cancellations, dispatch_cancellations, publish_cancellation

from test_delivery_integration import DATABASE_URL, REDIS_URL, WORKER, cleanup_run, create_run, dispatch_once, services
from test_worker_recovery_integration import events_for, first_image, input_fixture, project_run, read_resp, terminal_event, wait_for, worker_process

pytestmark = pytest.mark.skipif(not DATABASE_URL or not REDIS_URL, reason="disposable services required")


def test_cancel_request_is_durable_and_idempotent(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path)
            app, record = await create_run(database, redis, token, tmp_path)
            try:
                async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                    path = f"/api/v1/runs/{record['id']}/cancel"
                    first = await client.post(path)
                    print("cancel response:", first.status_code, first.text)
                    assert first.status_code == 202
                    assert first.json()["cancel_requested"]
                    assert (await client.post(path)).json()["cancel_requested"]
                    assert (await database.get_run(record["id"]))["cancel_requested_at"] is not None
                    assert (await client.post("/api/v1/runs/missing/cancel")).status_code == 404
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


@pytest.mark.skipif(not WORKER, reason="C++ Worker required")
@pytest.mark.parametrize("position", ["outbox", "queued", "abandoned"])
def test_cancel_before_processing_skips_models_and_survives_reclaim(tmp_path, position):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path)
            app, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            try:
                if position != "outbox":
                    await dispatch_once(redis, database, stream, 10)
                if position == "abandoned":
                    await redis.xgroup_create(stream, "document-workers", id="0")
                    await redis.xreadgroup("document-workers", "dead", {stream: ">"})
                async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                    response = await client.post(f"/api/v1/runs/{record['id']}/cancel")
                    assert response.status_code == 202
                    assert response.json()["status"] == "queued"
                await dispatch_once(redis, database, stream, 10)
                with worker_process(tmp_path, stream, token):
                    terminal = await wait_for(lambda: terminal_event(redis, record))
                    assert terminal["type"] == "job_cancelled"
                    assert [e["type"] for e in await events_for(redis, record)] == ["job_cancelled"]
                    assert (await redis.xpending(stream, "document-workers"))["pending"] == 0
                    assert not list(Path(record["job_path"]).parent.glob("executions/*/output"))
                await project_run(redis, database, record)
                await redis.delete(f"run:{record['id']}")
                persisted = await database.get_run(record["id"])
                assert persisted["status"] == "cancelled" and persisted["error"] is None
                assert await redis.get(cancellation_key(record["id"])) == record["attempt_id"]
                assert await cleanup_cancellations(redis, database)
                assert not await redis.exists(cancellation_key(record["id"]))
                assert not await dispatch_cancellations(redis, database)
                print(position, "-> one cancelled event, no processing, pending=0, durable cleanup")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_cancel_during_redis_outage_retries_unknown_delivery_without_resurrection(tmp_path):
    class UnavailableRedis:
        async def eval(self, *args):
            raise RedisConnectionError("injected cancellation delivery outage")
        async def hgetall(self, key):
            raise RedisConnectionError("injected cancellation state outage")

    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path)
            app, record = await create_run(database, redis, token, tmp_path)
            try:
                app.state.redis = UnavailableRedis()
                async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                    response = await client.post(f"/api/v1/runs/{record['id']}/cancel")
                    assert response.status_code == 202 and response.json()["cancel_requested"]
                assert (await database.get_run(record["id"]))["cancel_delivered_at"] is None
                async def lost_reply(run, attempt):
                    await publish_cancellation(redis, run, attempt)
                    raise RedisConnectionError("injected lost cancellation reply after commit")
                with pytest.raises(RedisConnectionError):
                    await database.dispatch_next_cancellation(lost_reply)
                assert await redis.ttl(cancellation_key(record["id"])) == -1
                assert sum(await asyncio.gather(dispatch_cancellations(redis, database),
                                               dispatch_cancellations(redis, database))) == 1
                # Terminal projection and cleanup win even if a retry follows.
                await database.update_run(record["id"], record["attempt_id"], 5, "cancelled", None, None)
                assert await cleanup_cancellations(redis, database)
                assert not await dispatch_cancellations(redis, database)
                assert not await redis.exists(cancellation_key(record["id"]))
                print("Redis outage/lost cancellation reply -> durable retry -> no marker resurrection")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


@pytest.mark.skipif(not WORKER, reason="C++ Worker required")
@pytest.mark.parametrize("crash", [False, True], ids=["running", "crashed"])
def test_cancel_running_or_crashed_execution_and_reuse_worker(tmp_path, crash):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path, "pdfjs-tracemonkey.pdf")
            app, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            try:
                await dispatch_once(redis, database, stream, 10)
                with worker_process(tmp_path, stream, token, WORKER_JOB_LEASE_MS="1000" if crash else "30000") as original:
                    await wait_for(lambda: first_image(redis, record))
                    original.send_signal(signal.SIGKILL if crash else signal.SIGSTOP)
                    if crash:
                        await asyncio.to_thread(original.wait, timeout=5)
                    async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                        response = await client.post(f"/api/v1/runs/{record['id']}/cancel")
                        assert response.status_code == 202 and response.json()["cancel_requested"]
                    before = await events_for(redis, record)
                    if not crash:
                        original.send_signal(signal.SIGCONT)
                    with worker_process(tmp_path, stream, token + "-replacement") if crash else nullcontext():
                        terminal = await wait_for(lambda: terminal_event(redis, record))
                        assert terminal["type"] == "job_cancelled"
                        after = await events_for(redis, record)
                        assert after[:-1] == before
                        assert terminal["execution_id"] == ("execution_2" if crash else "execution_1")
                        assert (await redis.xpending(stream, "document-workers"))["pending"] == 0
                        if not crash:
                            assert original.poll() is None
                        # Follow-up work still succeeds in the same Worker pool.
                        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                            response = await client.post(f"/api/v1/documents/{record['document_id']}/runs", json={
                                "dpi": 72, "backends": {"ocr": "noop", "layout": "text", "table": "text"},
                            })
                        followup = await database.get_run(response.json()["run_id"])
                        try:
                            await dispatch_once(redis, database, stream, 10)
                            assert (await wait_for(lambda: terminal_event(redis, followup)))["type"] == "job_succeeded"
                        finally:
                            await cleanup_run(redis, followup)
                await project_run(redis, database, record)
                assert (await database.get_run(record["id"]))["status"] == "cancelled"
                print("crash=", crash, "-> cancellation survives execution transition; follow-up succeeds")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


@pytest.mark.skipif(not WORKER, reason="C++ Worker required")
@pytest.mark.parametrize("winner", ["cancel", "success"])
def test_cancellation_and_completion_race_has_one_terminal_outcome(tmp_path, winner):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path)
            app, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            arrived, release = asyncio.Event(), asyncio.Event()
            tasks = set()
            async def proxy(reader, writer):
                task = asyncio.current_task()
                tasks.add(task)
                remote = None
                try:
                    address = urlparse(REDIS_URL)
                    replies, remote = await asyncio.open_connection(address.hostname, address.port or 6379)
                    while True:
                        raw, command = await read_resp(reader)
                        success = any(isinstance(arg, bytes) and arg.startswith(b'{') and
                                      json.loads(arg).get("type") == "job_succeeded" for arg in command)
                        if success and winner == "cancel":
                            arrived.set()
                            await release.wait()
                        remote.write(raw)
                        await remote.drain()
                        response, _ = await read_resp(replies)
                        if success and winner == "success":
                            arrived.set()
                            await release.wait()
                        writer.write(response)
                        await writer.drain()
                except (EOFError, ConnectionError, asyncio.IncompleteReadError):
                    pass
                finally:
                    if remote:
                        remote.close()
                    writer.close()
                    tasks.discard(task)
            server = await asyncio.start_server(proxy, "127.0.0.1", 0)
            try:
                await dispatch_once(redis, database, stream, 10)
                with worker_process(tmp_path, stream, token, REDIS_PORT=str(server.sockets[0].getsockname()[1])):
                    await asyncio.wait_for(arrived.wait(), timeout=15)
                    async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                        response = await client.post(f"/api/v1/runs/{record['id']}/cancel")
                        assert response.status_code == 202
                        if winner == "success":
                            assert response.json()["status"] == "succeeded"
                            assert not await redis.exists(cancellation_key(record["id"]))
                    release.set()
                    expected = "job_cancelled" if winner == "cancel" else "job_succeeded"
                    terminal = await wait_for(lambda: terminal_event(redis, record))
                    assert terminal["type"] == expected
                    events = await events_for(redis, record)
                    assert sum(e["type"] in {"job_succeeded", "job_failed", "job_cancelled"} for e in events) == 1
                    assert (await redis.xpending(stream, "document-workers"))["pending"] == 0
                    await project_run(redis, database, record)
                    await redis.delete(f"run:{record['id']}")
                    async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                        for _ in range(2):
                            response = await client.post(f"/api/v1/runs/{record['id']}/cancel")
                            assert response.json()["status"] == ("cancelled" if winner == "cancel" else "succeeded")
                    assert await events_for(redis, record) == events
                    await cleanup_cancellations(redis, database)
                    assert not await redis.exists(cancellation_key(record["id"]))
                    print("race winner=", winner, "->", expected, "; repeat cancel preserves terminal state")
            finally:
                release.set()
                server.close()
                await server.wait_closed()
                remaining = list(tasks)
                for task in remaining:
                    task.cancel()
                await asyncio.gather(*remaining, return_exceptions=True)
                await cleanup_run(redis, record)
    asyncio.run(check())
