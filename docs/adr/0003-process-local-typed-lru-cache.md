# ADR-0003: Process-Local Typed LRU Cache

## Status

Accepted

## Context

Post reads benefit from bounded caching, while the educational integer caches do not
provide the domain typing, expiration, or thread safety required by request handlers.

## Decision

Use a mutex-protected LRU cache of posts keyed by MongoDB ID, with configurable entry
capacity and TTL. Keep one cache per process and retain the integer LRU, LFU, and
random-replacement implementations only as examples.

## Consequences

The design is simple, bounded, and has no external cache dependency. Cache state and
metrics are local to each replica, so stale reads can persist until TTL expiration
after a mutation handled elsewhere.
