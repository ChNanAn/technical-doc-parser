from __future__ import annotations

import asyncio
import json
from contextlib import suppress
from typing import Any

from app import projector
from app.main import (
    PIPELINE_DEBUG_EXTENSION,
    _document_stage_output,
    _enqueue_job,
)
from app.projector import (
    PLATFORM_EVENT_STREAM,
    PROJECTOR_CONSUMER_GROUP,
    ProjectorState,
    _project_entry,
    _read_projector_entries,
    supervise_worker_event_projector,
)


class RecordingRedis:
    def __init__(self) -> None:
        self.acknowledged: list[tuple[str, str, str]] = []

    async def xack(self, stream: str, group: str, message_id: str) -> None:
        self.acknowledged.append((stream, group, message_id))


class RecordingDatabase:
    def __init__(self, error: Exception | None = None) -> None:
        self.error = error
        self.updates: list[tuple[str, str, str | None, str | None]] = []

    async def update_run(self, run_id: str, status: str, stage: str | None, error: str | None) -> None:
        if self.error is not None:
            raise self.error
        self.updates.append((run_id, status, stage, error))


def test_stage_output_reads_document_v1_page_and_debug_fields() -> None:
    document = {
        "pages": [
            {
                "number": 7,
                "page_number": 99,
                "image": {"uri": "pages/page_7.png"},
                "debug": {"layout": {"legacy": True}},
                "extensions": {
                    PIPELINE_DEBUG_EXTENSION: {
                        "layout": {"blocks": [{"id": "block_7"}]},
                        "tables": {"tables": []},
                    }
                },
            }
        ],
        "blocks": [{"id": "block_7"}],
    }

    assert _document_stage_output(document, "render") == [
        {"page_number": 7, "image": {"uri": "pages/page_7.png"}}
    ]
    assert _document_stage_output(document, "layout") == [
        {"page_number": 7, "output": {"blocks": [{"id": "block_7"}]}}
    ]
    assert _document_stage_output(document, "table") == [
        {"page_number": 7, "output": {"tables": []}}
    ]
    assert _document_stage_output(document, "export") == {"blocks": [{"id": "block_7"}]}


def test_job_queue_uses_an_approximate_stream_limit() -> None:
    class QueueRedis:
        def __init__(self) -> None:
            self.added: tuple[str, dict[str, str], dict[str, Any]] | None = None

        async def xadd(self, stream: str, fields: dict[str, str], **options: Any) -> None:
            self.added = (stream, fields, options)

    redis = QueueRedis()
    fields = {"job_id": "job_1", "run_id": "run_1", "job_path": "/runtime/job.json"}
    asyncio.run(_enqueue_job(redis, "document-jobs", fields, 10_000))  # type: ignore[arg-type]

    assert redis.added == (
        "document-jobs",
        fields,
        {"maxlen": 10_000, "approximate": True},
    )


def test_projector_discards_and_acknowledges_malformed_events(caplog: Any) -> None:
    redis = RecordingRedis()
    database = RecordingDatabase()

    asyncio.run(_project_entry(redis, database, "1-0", {"event": "{invalid"}))  # type: ignore[arg-type]

    assert database.updates == []
    assert redis.acknowledged == [(PLATFORM_EVENT_STREAM, PROJECTOR_CONSUMER_GROUP, "1-0")]
    assert "message_id=1-0" in caplog.text


def test_projector_retries_database_failures_without_acknowledging() -> None:
    redis = RecordingRedis()
    database = RecordingDatabase(RuntimeError("database unavailable"))
    event = {
        "type": "stage_completed",
        "run_id": "run_1",
        "stage": "layout",
    }

    async def project() -> None:
        try:
            await _project_entry(  # type: ignore[arg-type]
                redis,
                database,
                "2-0",
                {"event": json.dumps(event)},
            )
        except RuntimeError as error:
            assert str(error) == "database unavailable"
        else:
            raise AssertionError("database failure should be propagated to the supervisor")

    asyncio.run(project())
    assert redis.acknowledged == []


def test_projector_replays_its_pending_messages_before_reading_new_events() -> None:
    class PendingRedis:
        def __init__(self) -> None:
            self.read_ids: list[str] = []

        async def xreadgroup(
            self,
            group: str,
            consumer: str,
            streams: dict[str, str],
            **_: Any,
        ) -> list[tuple[str, list[tuple[str, dict[str, str]]]]]:
            assert group == PROJECTOR_CONSUMER_GROUP
            assert consumer == "consumer-1"
            stream_id = streams[PLATFORM_EVENT_STREAM]
            self.read_ids.append(stream_id)
            if stream_id == "0":
                return [(PLATFORM_EVENT_STREAM, [("3-0", {"event": "{}"})])]
            raise AssertionError("new events must not be read while this consumer has pending work")

        async def xautoclaim(self, *_: Any, **__: Any) -> Any:
            raise AssertionError("orphan claiming must follow own pending replay")

    redis = PendingRedis()
    entries = asyncio.run(_read_projector_entries(redis, "consumer-1", 30_000))  # type: ignore[arg-type]

    assert entries == [("3-0", {"event": "{}"})]
    assert redis.read_ids == ["0"]


def test_projector_claims_abandoned_pending_messages_before_new_events() -> None:
    class OrphanedRedis:
        def __init__(self) -> None:
            self.claimed = False

        async def xreadgroup(
            self,
            group: str,
            consumer: str,
            streams: dict[str, str],
            **_: Any,
        ) -> list[tuple[str, list[tuple[str, dict[str, str]]]]]:
            assert streams == {PLATFORM_EVENT_STREAM: "0"}
            return []

        async def xautoclaim(
            self,
            stream: str,
            group: str,
            consumer: str,
            minimum_idle: int,
            **options: Any,
        ) -> list[Any]:
            assert (stream, group, consumer) == (
                PLATFORM_EVENT_STREAM,
                PROJECTOR_CONSUMER_GROUP,
                "consumer-2",
            )
            assert minimum_idle == 30_000
            assert options == {"start_id": "0-0", "count": 50}
            self.claimed = True
            return ["0-0", [("4-0", {"event": "{}"})], []]

    redis = OrphanedRedis()
    entries = asyncio.run(_read_projector_entries(redis, "consumer-2", 30_000))  # type: ignore[arg-type]

    assert redis.claimed
    assert entries == [("4-0", {"event": "{}"})]


def test_projector_supervisor_restarts_after_transient_failure(monkeypatch: Any) -> None:
    attempts = 0
    restarted = asyncio.Event()

    async def run_projector(*_: Any) -> None:
        nonlocal attempts
        attempts += 1
        if attempts == 1:
            raise RuntimeError("temporary Redis failure")
        restarted.set()
        await asyncio.Future()

    monkeypatch.setattr(projector, "_project_worker_events", run_projector)

    async def supervise() -> None:
        state = ProjectorState()
        task = asyncio.create_task(
            supervise_worker_event_projector(
                object(),  # type: ignore[arg-type]
                object(),  # type: ignore[arg-type]
                "consumer-1",
                state,
                30_000,
                0.001,
            )
        )
        await asyncio.wait_for(restarted.wait(), timeout=1)
        assert attempts == 2
        assert state.active
        assert state.restart_count == 1
        assert state.last_error is None
        task.cancel()
        with suppress(asyncio.CancelledError):
            await task
        assert not state.active

    asyncio.run(supervise())
