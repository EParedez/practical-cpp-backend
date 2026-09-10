# Production Post Cache

The production read cache is a thread-safe, typed LRU cache keyed by MongoDB post ID.
The original integer LRU, LFU, and random-replacement implementations remain in the
repository as educational examples and are not used by request handlers. The random
replacement example now maintains a key-to-index map, so its eviction path is O(1).

## Policy

- Capacity is configured with `CACHE_CAPACITY`; zero disables storage safely.
- Negative capacity is rejected during configuration parsing.
- TTL is configured with `CACHE_TTL_SECONDS` and defaults to 60 seconds.
- Entries are copied into and out of the cache, so callers cannot mutate shared state.
- A mutex protects lookup, LRU promotion, insertion, eviction, and invalidation.
- Successful creates and updates populate or refresh an entry.
- Successful deletes invalidate the entry immediately.
- Expired entries behave as misses and are removed during lookup.

Each HTTP or gRPC process owns its own cache. A mutation refreshes the cache in the
process that handled it; another process can retain its previous value only until the
configured TTL. Deployments requiring immediate cross-process coherence should use a
shared cache or publish invalidation events.

## Memory Bound

The cache is bounded by entry count. Post validation caps content at 1 MiB, title at
200 bytes, author at 100 bytes, 50 tags, and 100 embedded comments, so the configured
capacity also places an upper bound on retained application data. Choose a capacity
that fits the container memory limit; `128` is the default.

## Metrics

The process-local Prometheus endpoint exposes:

- `blog_cache_hits_total`
- `blog_cache_misses_total`
- `blog_cache_evictions_total`
- `blog_cache_invalidations_total`

HTTP metrics are available at `/metrics`. The gRPC process exposes its metrics on the
internal listener at port 9090 by default.
