from __future__ import annotations

import asyncio
import logging

from redis.asyncio import Redis

from .database import Database


LOGGER = logging.getLogger(__name__)
REQUEST_CANCELLATION = """
local status = redis.call('HGET', KEYS[1], 'status')
if status == 'succeeded' or status == 'failed' or status == 'cancelled' then return 0 end
-- No TTL while outstanding: long calls, outages and crash redelivery must not
-- forget a cancellation. Cleanup follows durable terminal projection.
redis.call('SET', KEYS[2], ARGV[1])
return 1
"""


def cancellation_key(run_id: str) -> str:
    return f"run-cancel:{run_id}"


async def publish_cancellation(redis: Redis, run_id: str, attempt_id: str) -> None:
    delivered = await asyncio.wait_for(redis.eval(
        REQUEST_CANCELLATION, 2, f"run:{run_id}", cancellation_key(run_id), attempt_id,
    ), timeout=5)
    LOGGER.info("cancel delivery run_id=%s attempt_id=%s active=%s", run_id, attempt_id, delivered)


async def dispatch_cancellations(redis: Redis, database: Database, run_id: str | None = None) -> bool:
    return await database.dispatch_next_cancellation(
        lambda run, attempt: publish_cancellation(redis, run, attempt), run_id,
    )


async def cleanup_cancellations(redis: Redis, database: Database) -> bool:
    async def remove(run_id: str) -> None:
        await asyncio.wait_for(redis.delete(cancellation_key(run_id)), timeout=5)
    return await database.cleanup_next_cancellation(remove)
