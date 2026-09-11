# Architecture

This document describes the implemented architecture of PracticalCppBackend. Planned
work is intentionally separated under [Future Work](#future-work); it should not be
read as a description of current behavior.

## System Context

The application is a C++17 blog backend with two API processes over one MongoDB data
model:

```text
Browser or HTTP client
        │
        ▼
      Nginx ───────────────► HTTP server :8080
                                  │
Internal gRPC client ──────► gRPC server :50051
                                  │
                                  ▼
                         BlogStore interface
                                  │
                                  ▼
                     BlogRepository / MongoDB
```

HTTP and gRPC are separate processes. Each creates its own repository connection pool,
typed post cache, authentication objects, limiter, logs, and metrics. They share data
through MongoDB, not through process memory.

## Runtime Components

| Component | Source | Responsibility |
| --- | --- | --- |
| `blogcore` | `src/auth`, `src/cache`, `src/common`, `src/config`, `src/db`, `src/model` | Domain validation, security, telemetry, caching, and persistence |
| `blogservice` | `src/api/blog_service_impl.*` and generated protobuf code | gRPC request validation, authorization, and repository mapping |
| `bloghttp` | `src/server/http_app.*` | HTTP routing, JSON, authorization, CORS, and response mapping |
| `blog_http_server` | `src/server/http_server_main.cpp` | HTTP/HTTPS process lifecycle and graceful shutdown |
| `blog_grpc_server` | `src/api/grpc_server_main.cpp` | gRPC/TLS lifecycle plus an internal HTTP health/metrics listener |
| `blog_grpc_client` | `src/api/grpc_client_main.cpp` | Example CRUD client |

The `BlogStore` interface separates transport behavior from MongoDB. HTTP and gRPC
tests substitute an in-memory or purpose-built test store without connecting to a
database.

## Request Flow

1. The transport accepts a bounded request and assigns or validates a request ID.
2. CORS, credentials, role requirements, and rate limits are evaluated where relevant.
3. The API layer validates and converts the request into domain models.
4. Reads check the process-local post cache before calling `BlogStore`.
5. `BlogRepository` acquires a pooled MongoDB client and performs the operation.
6. Successful mutations refresh or invalidate the local cache.
7. The transport maps typed repository results to HTTP or gRPC status codes.
8. Structured logs and process-local counters record the outcome and duration.

## Persistence

`BlogRepository` is the only production component that uses `mongocxx` directly. At
startup it runs idempotent schema migration version 1, installs strict collection
validators, creates required indexes, and verifies them before accepting traffic.

Posts embed a bounded comments array. Dates are stored as native BSON dates but remain
canonical UTC strings at the public API boundary. List queries are bounded, filtered,
and sorted by publication date and MongoDB ID in descending order. Details are in
[PERSISTENCE.md](PERSISTENCE.md).

## Cache and Consistency

The production cache is a mutex-protected, typed LRU cache keyed by post ID. Its
capacity and TTL are configurable. A capacity of zero safely disables storage.

Because HTTP and gRPC run separately, cache coherence is eventual across processes:
a mutation immediately changes only the cache in the process that handled it. Other
instances can retain an old entry until its TTL expires. Strong cross-instance cache
coherence would require shared storage or invalidation events. See [CACHE.md](CACHE.md).

The integer LRU, LFU, and random-replacement classes remain educational examples and
are not in the request path.

## Security Boundaries

Role-based Bearer tokens protect mutation endpoints when `AUTH_REQUIRED=true`.
Readers can optionally be required for read endpoints. Passwords accepted by the
repository are immediately converted to salted PBKDF2-HMAC-SHA-256 hashes and hashes
are not returned by read models.

HTTP and gRPC can terminate TLS directly, or an external trusted load balancer can
terminate TLS while application listeners remain private. CORS is deny-by-default,
HTTP responses include restrictive security headers, and authenticated operations use
a process-local fixed-window rate limiter. Production constraints are documented in
[SECURITY.md](SECURITY.md).

## Operations and Deployment

- `/health` reports process liveness; `/ready` verifies MongoDB connectivity.
- HTTP exposes Prometheus text metrics on `/metrics`.
- gRPC exposes health and metrics on a separate internal HTTP listener.
- Logs are newline-delimited JSON with request IDs, status, operation, and duration.
- Docker Compose runs MongoDB, HTTP, gRPC, and Nginx for local verification.
- The supported cloud model is two ECS Fargate services using one immutable image,
  managed MongoDB, private networking, secret injection, and TLS load balancers.
- GitHub Actions gates publication on tests and security checks, then emits an SBOM,
  immutable image tag, and provenance attestation.

The AWS files are deployable templates, not evidence of an existing AWS deployment.
See [DEPLOYMENT.md](DEPLOYMENT.md) and [RELEASE.md](RELEASE.md).

## Build and Verification

CMake builds three runtime executables and three test executables. The suite currently
contains 73 tests: 51 unit tests and 22 MongoDB-backed integration tests. CI adds
warnings-as-errors builds on Ubuntu 22.04 and 24.04, clang-format, clang-tidy,
sanitizers, coverage, dependency review, secret scanning, `amd64`/`arm64` container
builds and scans, and an end-to-end Compose smoke test.

## Architecture Decisions

Short decision records explain the important boundaries and tradeoffs:

- [ADR-0001: Separate HTTP and gRPC processes](docs/adr/0001-separate-http-and-grpc-processes.md)
- [ADR-0002: MongoDB repository and startup migrations](docs/adr/0002-mongodb-repository-and-startup-migrations.md)
- [ADR-0003: Process-local typed LRU cache](docs/adr/0003-process-local-typed-lru-cache.md)
- [ADR-0004: Role-based static Bearer tokens](docs/adr/0004-role-based-static-bearer-tokens.md)
- [ADR-0005: ECS Fargate deployment model](docs/adr/0005-ecs-fargate-deployment-model.md)

## Future Work

The following items are exploratory and are not implemented:

- Deploy and validate the ECS templates in a real AWS environment.
- Introduce shared cache invalidation if cross-process stale reads are unacceptable.
- Benchmark HTTP against gRPC and compare cache policies under representative load.
- Evaluate a C++20 migration only after compiler and dependency compatibility checks.
- Add a dedicated identity provider if user-facing login replaces deployment tokens.

The authoritative phased backlog and completion history are in
[IMPROVEMENT_PLAN.md](IMPROVEMENT_PLAN.md).
