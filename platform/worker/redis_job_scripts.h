#pragma once

namespace doc_parser::platform::detail {

// Redis server time and one script per transition avoid client clock skew and
// races between a slow original owner, renewal, and another consumer's claim.
inline constexpr const char* reclaimJobScript = R"(
    local clock = redis.call('TIME')
    local now = clock[1] * 1000 + math.floor(clock[2] / 1000)
    local entries = redis.call('XPENDING', KEYS[1], ARGV[1], 'IDLE', ARGV[3], ARGV[4], '+', 32)
    local cursor = '-'
    for _, entry in ipairs(entries) do
        cursor = '(' .. entry[1]
        local key = ARGV[5] .. entry[1]
        local until_ms = tonumber(redis.call('HGET', key, 'until_ms') or '0')
        if until_ms <= now then
            local claimed = redis.call('XCLAIM', KEYS[1], ARGV[1], ARGV[2], ARGV[3], entry[1])
            if #claimed > 0 then return {cursor, claimed} end
        end
    end
    if #entries < 32 then cursor = '-' end
    return {cursor, {}}
)";

inline constexpr const char* acquireJobScript = R"(
    local clock = redis.call('TIME')
    local now = clock[1] * 1000 + math.floor(clock[2] / 1000)
    local pending = redis.call('XPENDING', KEYS[1], ARGV[1], ARGV[3], ARGV[3], 1)
    if #pending == 0 or pending[1][2] ~= ARGV[2] then return {} end
    if tonumber(redis.call('HGET', KEYS[2], 'until_ms') or '0') > now then return {} end
    local sequence = tonumber(redis.call('HGET', KEYS[2], 'sequence') or '0')
    -- Upgrade compatibility: the previous worker could publish terminal state
    -- before losing its XACK response. Never restart such completed work.
    local previous = redis.call('HGET', KEYS[3], 'last_event')
    if previous then
        local ok, event = pcall(cjson.decode, previous)
        if ok and type(event) == 'table' and event.attempt_id == ARGV[5] then
            if event.type == 'job_succeeded' or event.type == 'job_failed' or event.type == 'job_cancelled' then
                redis.call('XACK', KEYS[1], ARGV[1], ARGV[3])
                redis.call('DEL', KEYS[2])
                return {}
            end
            sequence = math.max(sequence, tonumber(event.sequence) or 0)
        end
    end
    local generation = redis.call('HINCRBY', KEYS[2], 'generation', 1)
    redis.call('HSET', KEYS[2], 'consumer', ARGV[2], 'until_ms', now + ARGV[4], 'sequence', sequence)
    -- No expiry while pending: losing this metadata would reset sequence/fencing.
    redis.call('PERSIST', KEYS[2])
    return {generation, sequence}
)";

inline constexpr const char* renewJobScript = R"(
    local clock = redis.call('TIME')
    local now = clock[1] * 1000 + math.floor(clock[2] / 1000)
    if redis.call('HGET', KEYS[1], 'generation') ~= ARGV[1] or
       redis.call('HGET', KEYS[1], 'consumer') ~= ARGV[2] or
       tonumber(redis.call('HGET', KEYS[1], 'until_ms') or '0') <= now then return 0 end
    redis.call('HSET', KEYS[1], 'until_ms', now + ARGV[3])
    return 1
)";

} // namespace doc_parser::platform::detail
