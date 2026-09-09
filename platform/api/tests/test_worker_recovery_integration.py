"""Crash/reclaim tests against a real Worker and disposable Redis/PostgreSQL."""
from __future__ import annotations

import asyncio
import hashlib
import json
import os
import shutil
import signal
import subprocess
from contextlib import contextmanager
from pathlib import Path
from urllib.parse import urlparse

import pytest
import httpx
import jsonschema

from test_delivery_integration import (
    DATABASE_URL, REDIS_URL, WORKER, cleanup_run, create_run, dispatch_once, services,
)
from app.projector import _project_entry

pytestmark = pytest.mark.skipif(not DATABASE_URL or not REDIS_URL or not WORKER,
                                reason="disposable services and C++ Worker required")


@contextmanager
def worker_process(root, stream, name, **overrides):
    address = urlparse(REDIS_URL)
    env = dict(os.environ, REDIS_HOST=address.hostname, REDIS_PORT=str(address.port or 6379),
               JOB_STREAM=stream, WORKER_ID=name, WORKER_RUNTIME_ROOT=str(root),
               WORKER_JOB_LEASE_MS="1000", WORKER_JOB_MAX_EXECUTIONS="3")
    for model in ["PADDLEOCR_MODEL_DIR", "DOCLAYNET_MODEL", "PADDLE_LAYOUT_MODEL",
                  "TABLE_DETECTION_MODEL", "TABLE_STRUCTURE_MODEL"]:
        env["DOCUMENT_INTELLIGENCE_ENGINE_" + model] = str(root / "no-model")
    env.update(overrides)
    with (root / f"{name}.log").open("w") as log:
        process = subprocess.Popen([WORKER], env=env, stdout=log, stderr=log)
        try:
            yield process
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=10)


async def wait_for(check, seconds=15):
    deadline = asyncio.get_running_loop().time() + seconds
    while asyncio.get_running_loop().time() < deadline:
        result = await check()
        if result:
            return result
        await asyncio.sleep(0.025)
    pytest.fail("condition did not become true before deadline")


async def events_for(redis, record):
    return [json.loads(fields["event"]) for _, fields in await redis.xrange(f"run-events:{record['id']}")]


async def terminal_event(redis, record):
    events = await events_for(redis, record)
    return events[-1] if events and events[-1]["type"] in {"job_succeeded", "job_failed"} else None


def input_fixture(root, name="pdfjs-basicapi.pdf"):
    fixture = Path(__file__).resolve().parents[3] / "tests/fixtures/pdfs" / name
    shutil.copyfile(fixture, root / "input.pdf")


def test_worker_recovers_message_abandoned_before_execution(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path)
            _, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            try:
                await dispatch_once(redis, database, stream, 10)
                await redis.xgroup_create(stream, "document-workers", id="0")
                await redis.xreadgroup("document-workers", "dead-worker", {stream: ">"}, count=1)
                print("fault injected:", await redis.xpending(stream, "document-workers"), flush=True)
                with worker_process(tmp_path, stream, token), worker_process(tmp_path, stream, token + "-contender"):
                    event = await wait_for(lambda: terminal_event(redis, record), seconds=8)
                    assert event["type"] == "job_succeeded"
                    assert (await redis.xpending(stream, "document-workers"))["pending"] == 0
                    assert sum(e["type"] == "job_started" for e in await events_for(redis, record)) == 1
                    print("recovered:", event["type"], "pending=0", flush=True)
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


async def first_image(redis, record):
    events = await events_for(redis, record)
    return next((event for event in events if event["type"] == "artifact_ready"), None)


async def project_run(redis, database, record):
    for event_id, fields in await redis.xrange("platform-events"):
        if record["id"] in fields.get("event", ""):
            await _project_entry(redis, database, event_id, fields)


@pytest.mark.parametrize("pause", [False, True], ids=["SIGKILL", "SIGSTOP-and-resume"])
def test_crashed_or_paused_worker_is_fenced_and_replacement_output_is_durable(tmp_path, pause):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path, "pdfjs-tracemonkey.pdf")
            app, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            try:
                await dispatch_once(redis, database, stream, 10)
                with worker_process(tmp_path, stream, token + "-original") as original:
                    await wait_for(lambda: first_image(redis, record))
                    original.send_signal(signal.SIGSTOP if pause else signal.SIGKILL)
                    if not pause:
                        await asyncio.to_thread(original.wait, timeout=5)
                    async def replacement_finished():
                        event = await terminal_event(redis, record)
                        return event if event and event.get("execution_id") == "execution_2" else None
                    with worker_process(tmp_path, stream, token + "-replacement"):
                        terminal = await wait_for(replacement_finished)
                        assert terminal["type"] == "job_succeeded"
                        assert (await redis.xpending(stream, "document-workers"))["pending"] == 0
                        winner = Path(record["job_path"]).parent / "executions/execution_2"
                        before = {str(path.relative_to(winner)): hashlib.sha256(path.read_bytes()).hexdigest()
                                  for path in winner.rglob("*") if path.is_file()}
                        accepted = await events_for(redis, record)
                        if pause:
                            original.send_signal(signal.SIGCONT)
                            await asyncio.to_thread(original.wait, timeout=8)
                            assert original.returncode != 0
                            assert await events_for(redis, record) == accepted
                            assert before == {str(path.relative_to(winner)): hashlib.sha256(path.read_bytes()).hexdigest()
                                              for path in winner.rglob("*") if path.is_file()}
                            assert "LEASE_LOST" in (tmp_path / f"{token}-original.log").read_text() or \
                                   "lease was lost" in (tmp_path / f"{token}-original.log").read_text()
                        sequences = [event["sequence"] for event in accepted]
                        assert sequences == sorted(set(sequences))
                        assert len({event["event_id"] for event in accepted}) == len(accepted)
                        protocol = Path(__file__).resolve().parents[2] / "protocol/schemas"
                        event_validator = jsonschema.Draft202012Validator(json.loads((protocol / "event.v1.schema.json").read_text()))
                        for event in accepted:
                            event_validator.validate(event)
                        assert [event["execution_id"] for event in accepted if event["type"] == "job_started"] == \
                               ["execution_1", "execution_2"]
                        await project_run(redis, database, record)
                        await redis.delete(f"run:{record['id']}")
                        persisted = await database.get_run(record["id"])
                        assert persisted["status"] == "succeeded"
                        assert persisted["execution_id"] == "execution_2"
                        assert persisted["last_event_sequence"] == terminal["sequence"]
                        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
                            base = f"/api/v1/runs/{record['id']}"
                            assert (await client.get(base)).json()["execution_id"] == "execution_2"
                            manifests = (await client.get(base + "/artifacts")).json()
                            assert manifests and all(m["execution_id"] == "execution_2" for m in manifests)
                            artifact_validator = jsonschema.Draft202012Validator(json.loads((protocol / "artifact.v1.schema.json").read_text()))
                            for manifest in manifests:
                                artifact_validator.validate(manifest)
                            artifact = base + "/artifacts/artifact_export_document_json"
                            assert (await client.get(artifact + "?execution_id=execution_1")).status_code == 409
                            response = await client.get(artifact + "?execution_id=execution_2")
                            assert response.status_code == 200
                            assert len(response.json()["pages"]) == 14
                            assert (await client.get(base + "/stages/layout")).status_code == 200
                        print(f"pause={pause}: two executions, monotonic events, durable winner, stale writer fenced")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_healthy_long_running_worker_renews_lease_without_duplicate_execution(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path, "pdfjs-tracemonkey.pdf")
            _, record = await create_run(database, redis, token, tmp_path)
            path = Path(record["job_path"])
            job = json.loads(path.read_text())
            job["pipeline"]["dpi"] = 300
            path.write_text(json.dumps(job))
            stream = "jobs:" + token
            try:
                await dispatch_once(redis, database, stream, 10)
                with worker_process(tmp_path, stream, token + "-owner"):
                    await wait_for(lambda: first_image(redis, record))
                    started = asyncio.get_running_loop().time()
                    with worker_process(tmp_path, stream, token + "-contender"):
                        terminal = await wait_for(lambda: terminal_event(redis, record), seconds=25)
                        assert terminal["type"] == "job_succeeded"
                        assert terminal["execution_id"] == "execution_1"
                        assert asyncio.get_running_loop().time() - started > 1
                        events = await events_for(redis, record)
                        assert sum(event["type"] == "job_started" for event in events) == 1
                        assert not list(path.parent.glob("executions/execution_2"))
                        print("long-running owner: renewed beyond lease window, contender did not steal")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_repeated_worker_crashes_reach_bounded_terminal_failure(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path, "pdfjs-tracemonkey.pdf")
            _, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            try:
                await dispatch_once(redis, database, stream, 10)
                for generation in [1, 2]:
                    async def started():
                        events = await events_for(redis, record)
                        return any(e["type"] == "job_started" and e.get("execution_id") == f"execution_{generation}"
                                   for e in events)
                    with worker_process(tmp_path, stream, token + str(generation), WORKER_JOB_MAX_EXECUTIONS="2") as worker:
                        await wait_for(started)
                        worker.kill()
                        await asyncio.to_thread(worker.wait, timeout=5)
                with worker_process(tmp_path, stream, token + "last", WORKER_JOB_MAX_EXECUTIONS="2"):
                    event = await wait_for(lambda: terminal_event(redis, record))
                    assert event["type"] == "job_failed"
                    assert event["error"]["code"] == "worker.recovery_exhausted"
                    assert event["execution_id"] == "execution_3"
                    assert not (Path(record["job_path"]).parent / "executions/execution_3/output").exists()
                    assert (await redis.xpending(stream, "document-workers"))["pending"] == 0
                    await project_run(redis, database, record)
                    assert (await database.get_run(record["id"]))["status"] == "failed"
                    print("two crashed executions -> recovery_exhausted + terminal ACK")
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


def test_claim_cursor_advances_past_a_full_batch_of_live_leases(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path)
            _, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            try:
                await redis.xgroup_create(stream, "document-workers", id="0", mkstream=True)
                live_ids = [await redis.xadd(stream, {"job_path": "/unused"}) for _ in range(40)]
                await redis.xreadgroup("document-workers", "live-worker", {stream: ">"}, count=40)
                now = await redis.time()
                for message_id in live_ids:
                    await redis.hset(f"job-execution:{stream}:document-workers:{message_id}", mapping={
                        "generation": 1, "consumer": "live-worker", "until_ms": now[0] * 1000 + 60000,
                    })
                await dispatch_once(redis, database, stream, 100)
                await redis.xreadgroup("document-workers", "dead-worker", {stream: ">"}, count=1)
                with worker_process(tmp_path, stream, token):
                    event = await wait_for(lambda: terminal_event(redis, record))
                    assert event["type"] == "job_succeeded"
                    pending = await redis.xpending_range(stream, "document-workers", "-", "+", 100)
                    assert len(pending) == 40 and all(p["consumer"] == "live-worker" for p in pending)
            finally:
                await cleanup_run(redis, record)
    asyncio.run(check())


async def read_resp(reader):
    """Retain wire bytes so the fault proxy forwards real Worker commands unchanged."""
    line = await reader.readline()
    if not line:
        raise EOFError
    prefix, value = line[:1], line[1:-2]
    if prefix == b"*":
        children = [await read_resp(reader) for _ in range(int(value))]
        return line + b"".join(raw for raw, _ in children), [item for _, item in children]
    if prefix == b"$":
        size = int(value)
        if size < 0:
            return line, None
        payload = await reader.readexactly(size + 2)
        return line + payload, payload[:-2]
    return line, value


def test_lost_terminal_response_cannot_restart_or_regress_a_completed_run(tmp_path):
    async def check():
        async with services() as (database, redis, token):
            input_fixture(tmp_path)
            _, record = await create_run(database, redis, token, tmp_path)
            stream = "jobs:" + token
            tasks = set()
            dropped = asyncio.Event()
            async def proxy(client_reader, client_writer):
                task = asyncio.current_task()
                tasks.add(task)
                remote_writer = None
                try:
                    address = urlparse(REDIS_URL)
                    remote_reader, remote_writer = await asyncio.open_connection(address.hostname, address.port or 6379)
                    while True:
                        raw, command = await read_resp(client_reader)
                        terminal = any(isinstance(arg, bytes) and arg.startswith(b'{') and
                                       json.loads(arg).get("type") == "job_succeeded" for arg in command)
                        remote_writer.write(raw)
                        await remote_writer.drain()
                        response, _ = await read_resp(remote_reader)
                        if terminal:
                            assert response == b":1\r\n"
                            dropped.set()
                            return  # Redis committed; the Worker never receives its reply.
                        client_writer.write(response)
                        await client_writer.drain()
                except (EOFError, ConnectionError, asyncio.IncompleteReadError):
                    pass
                finally:
                    if remote_writer:
                        remote_writer.close()
                    client_writer.close()
                    tasks.discard(task)
            server = await asyncio.start_server(proxy, "127.0.0.1", 0)
            try:
                await dispatch_once(redis, database, stream, 10)
                port = str(server.sockets[0].getsockname()[1])
                with worker_process(tmp_path, stream, token + "-lost-reply", REDIS_PORT=port) as original:
                    await asyncio.wait_for(dropped.wait(), timeout=15)
                    await asyncio.to_thread(original.wait, timeout=8)
                    assert original.returncode != 0
                accepted = await events_for(redis, record)
                assert accepted[-1]["type"] == "job_succeeded"
                assert (await redis.xpending(stream, "document-workers"))["pending"] == 0
                with worker_process(tmp_path, stream, token + "-restart"):
                    async def ready():
                        return [key async for key in redis.scan_iter(match=f"worker:{token}-restart-*")]
                    await wait_for(ready)
                    await asyncio.sleep(1.2)
                    assert await events_for(redis, record) == accepted
                await project_run(redis, database, record)
                assert (await database.get_run(record["id"]))["status"] == "succeeded"
                print("terminal Redis reply dropped: one success, pending=0, restart does not rerun")
            finally:
                server.close()
                await server.wait_closed()
                remaining = list(tasks)
                for task in remaining:
                    task.cancel()
                await asyncio.gather(*remaining, return_exceptions=True)
                await cleanup_run(redis, record)
    asyncio.run(check())
