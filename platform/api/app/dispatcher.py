from __future__ import annotations

import asyncio
import logging

from redis.asyncio import Redis

from .database import Database
from .cancellation import cleanup_cancellations, dispatch_cancellations


LOGGER = logging.getLogger(__name__)

# Do not evict jobs which a consumer still needs. Once the stream is full, the
# durable outbox supplies backpressure. Trim only below every group's oldest
# pending entry (or its last delivered entry if it has no pending entries).
ENQUEUE_JOB = """
local expected = {'stream', 'string', 'hash'}
for i = 1, 3 do
    local kind = redis.call('TYPE', KEYS[i]).ok
    if kind ~= 'none' and kind ~= expected[i] then
        return redis.error_reply('WRONGTYPE job dispatch key ' .. KEYS[i])
    end
end
local previous = redis.call('GET', KEYS[2])
if previous then return previous end
local function before(a, b)
    local am, as = string.match(a, '(%d+)-(%d+)')
    local bm, bs = string.match(b, '(%d+)-(%d+)')
    return tonumber(am) < tonumber(bm) or
        (tonumber(am) == tonumber(bm) and tonumber(as) < tonumber(bs))
end
if redis.call('EXISTS', KEYS[1]) == 1 then
    local floor = nil
    for _, group in ipairs(redis.call('XINFO', 'GROUPS', KEYS[1])) do
        local fields = {}
        for i = 1, #group, 2 do fields[group[i]] = group[i + 1] end
        local pending = redis.call('XPENDING', KEYS[1], fields['name'])
        local bound = pending[1] > 0 and pending[2] or fields['last-delivered-id']
        if pending[1] == 0 then
            local ms, seq = string.match(bound, '(%d+)-(%d+)')
            bound = ms .. '-' .. string.format('%.0f', tonumber(seq) + 1)
        end
        if not floor or before(bound, floor) then floor = bound end
    end
    if floor and floor ~= '0-0' then redis.call('XTRIM', KEYS[1], 'MINID', floor) end
end
if redis.call('XLEN', KEYS[1]) >= tonumber(ARGV[1]) then return false end
local id = redis.call('XADD', KEYS[1], '*',
    'job_id', ARGV[2], 'run_id', ARGV[3], 'attempt_id', ARGV[4], 'job_path', ARGV[5])
redis.call('HSET', KEYS[3], 'status', 'queued', 'stage', '')
redis.call('SET', KEYS[2], id)
return id
"""


def marker_key(job_id: str) -> str:
    return f"job-dispatch:{job_id}"


async def enqueue_job(redis: Redis, stream: str, fields: dict[str, str], maximum_length: int) -> str | None:
    message_id = await asyncio.wait_for(
        redis.eval(
            ENQUEUE_JOB, 3, stream, marker_key(fields["job_id"]), f"run:{fields['run_id']}",
            maximum_length, fields["job_id"], fields["run_id"], fields["attempt_id"], fields["job_path"],
        ), timeout=5,
    )
    if message_id is not None:
        LOGGER.info("job dispatched job_id=%s run_id=%s stream=%s message_id=%s",
                    fields["job_id"], fields["run_id"], stream, message_id)
    return message_id


async def dispatch_once(redis: Redis, database: Database, stream: str, maximum_length: int) -> bool:
    cancelled = await dispatch_cancellations(redis, database)
    published = await database.dispatch_next_job(
        lambda fields: enqueue_job(redis, stream, fields, maximum_length),
    )
    # Only remove markers for transactions known to have committed. A crash
    # before cleanup is harmless: another dispatcher will finish the cleanup.
    for row in await database.published_outbox_jobs():
        await asyncio.wait_for(redis.delete(marker_key(row["job_id"])), timeout=5)
        await database.mark_outbox_marker_cleaned(row["job_id"])
    cleaned = await cleanup_cancellations(redis, database)
    return published or cancelled or cleaned


async def dispatch_jobs(redis: Redis, database: Database, stream: str, maximum_length: int) -> None:
    failures = 0
    while True:
        try:
            published = await dispatch_once(redis, database, stream, maximum_length)
            failures = 0
            if not published:
                await asyncio.sleep(0.5)
        except asyncio.CancelledError:
            raise
        except Exception:
            failures += 1
            delay = min(30.0, 2.0 ** min(failures - 1, 5))
            LOGGER.exception("job dispatch failed; durable outbox retained, retry_seconds=%s", delay)
            await asyncio.sleep(delay)
