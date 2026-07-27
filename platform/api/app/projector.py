from __future__ import annotations

import asyncio
import json
import logging
from dataclasses import dataclass
from typing import Any

from redis.asyncio import Redis
from redis.exceptions import ResponseError

from .database import Database


LOGGER = logging.getLogger(__name__)
PLATFORM_EVENT_STREAM = "platform-events"
PROJECTOR_CONSUMER_GROUP = "api-event-projectors"

STATE_EVENT_STATUSES = {
    "job_started": "running",
    "stage_started": "running",
    "stage_progress": "running",
    "artifact_ready": "running",
    "stage_completed": "running",
    "stage_failed": "failed",
    "job_succeeded": "succeeded",
    "job_failed": "failed",
    "job_cancelled": "cancelled",
}
INFORMATION_EVENT_TYPES = {
    "run_configured",
    "stage_warning",
}
SUPPORTED_EVENT_TYPES = frozenset(STATE_EVENT_STATUSES) | INFORMATION_EVENT_TYPES


@dataclass
class ProjectorState:
    active: bool = False
    restart_count: int = 0
    last_error: str | None = None


def _stream_entries(messages: Any) -> list[tuple[str, dict[str, str]]]:
    return [entry for _, entries in messages for entry in entries]


async def _read_projector_entries(
    redis: Redis,
    consumer: str,
    claim_idle_milliseconds: int,
) -> list[tuple[str, dict[str, str]]]:
    pending = await redis.xreadgroup(
        PROJECTOR_CONSUMER_GROUP,
        consumer,
        {PLATFORM_EVENT_STREAM: "0"},
        count=50,
    )
    entries = _stream_entries(pending)
    if entries:
        return entries

    claimed = await redis.xautoclaim(
        PLATFORM_EVENT_STREAM,
        PROJECTOR_CONSUMER_GROUP,
        consumer,
        claim_idle_milliseconds,
        start_id="0-0",
        count=50,
    )
    if len(claimed) > 1 and claimed[1]:
        return claimed[1]

    messages = await redis.xreadgroup(
        PROJECTOR_CONSUMER_GROUP,
        consumer,
        {PLATFORM_EVENT_STREAM: ">"},
        count=50,
        block=5000,
    )
    return _stream_entries(messages)


@dataclass(frozen=True)
class RunProjection:
    run_id: str
    attempt_id: str
    sequence: int
    status: str
    stage: str | None
    error: str | None


def _projected_run_state(event: dict[str, Any]) -> RunProjection | None:
    event_type = event["type"]
    run_id = event["run_id"]
    attempt_id = event["attempt_id"]
    sequence = event["sequence"]
    if not isinstance(event_type, str) or event_type not in SUPPORTED_EVENT_TYPES:
        raise ValueError(f"unsupported event type: {event_type!r}")
    if not isinstance(run_id, str) or not run_id:
        raise ValueError("run_id must be a non-empty string")
    if not isinstance(attempt_id, str) or not attempt_id:
        raise ValueError("attempt_id must be a non-empty string")
    if not isinstance(sequence, int) or isinstance(sequence, bool) or sequence < 1:
        raise ValueError("sequence must be a positive integer")
    stage = event.get("stage")
    if stage is not None and not isinstance(stage, str):
        raise ValueError("stage must be a string")
    error_payload = event.get("error")
    if error_payload is not None and not isinstance(error_payload, dict):
        raise ValueError("error must be an object")
    error = error_payload.get("message") if isinstance(error_payload, dict) else None
    if event_type in INFORMATION_EVENT_TYPES:
        return None
    return RunProjection(
        run_id=run_id,
        attempt_id=attempt_id,
        sequence=sequence,
        status=STATE_EVENT_STATUSES[event_type],
        stage=stage,
        error=error,
    )


async def _project_entry(redis: Redis, database: Database, message_id: str, fields: dict[str, str]) -> None:
    try:
        encoded = fields["event"]
        event = json.loads(encoded)
        if not isinstance(event, dict):
            raise ValueError("event payload must be an object")
        projection = _projected_run_state(event)
    except (json.JSONDecodeError, KeyError, TypeError, ValueError) as reason:
        LOGGER.error(
            "discarding malformed worker event stream=%s message_id=%s reason=%s",
            PLATFORM_EVENT_STREAM,
            message_id,
            reason,
        )
        await redis.xack(PLATFORM_EVENT_STREAM, PROJECTOR_CONSUMER_GROUP, message_id)
        return

    if projection is not None:
        await database.update_run(
            projection.run_id,
            projection.attempt_id,
            projection.sequence,
            projection.status,
            projection.stage,
            projection.error,
        )
    await redis.xack(PLATFORM_EVENT_STREAM, PROJECTOR_CONSUMER_GROUP, message_id)


async def _project_worker_events(
    redis: Redis,
    database: Database,
    consumer: str,
    claim_idle_milliseconds: int,
) -> None:
    try:
        await redis.xgroup_create(
            PLATFORM_EVENT_STREAM,
            PROJECTOR_CONSUMER_GROUP,
            id="0",
            mkstream=True,
        )
    except ResponseError as error:
        if "BUSYGROUP" not in str(error):
            raise
    LOGGER.info("worker event projector started consumer=%s", consumer)
    while True:
        entries = await _read_projector_entries(redis, consumer, claim_idle_milliseconds)
        for message_id, fields in entries:
            await _project_entry(redis, database, message_id, fields)


async def supervise_worker_event_projector(
    redis: Redis,
    database: Database,
    consumer: str,
    state: ProjectorState,
    claim_idle_milliseconds: int,
    restart_delay_seconds: float,
) -> None:
    while True:
        state.active = True
        state.last_error = None
        try:
            await _project_worker_events(redis, database, consumer, claim_idle_milliseconds)
            raise RuntimeError("worker event projector stopped unexpectedly")
        except asyncio.CancelledError:
            state.active = False
            raise
        except Exception as error:
            state.active = False
            state.restart_count += 1
            state.last_error = str(error)
            LOGGER.exception(
                "worker event projector failed; restarting consumer=%s restart_count=%d delay_seconds=%s",
                consumer,
                state.restart_count,
                restart_delay_seconds,
            )
            await asyncio.sleep(restart_delay_seconds)
