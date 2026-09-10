# Testing Guide

The test suite is divided into unit and integration tests through CTest labels.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

## Unit Tests

Unit tests do not require MongoDB. They cover the cache implementations, HTTP routes
with an in-memory store, centralized configuration, and gRPC service behavior with a
test store. The current unit suite contains 29 tests.

```bash
ctest --test-dir build -L unit --output-on-failure
```

The HTTP tests bind to ephemeral ports on `127.0.0.1`. Environments that prohibit
local socket binding must explicitly allow loopback sockets for these tests.

## Integration Tests

Integration tests require MongoDB on `localhost:27017`. Start the local development
instance before running them:

```bash
./scripts/start_mongo.sh
ctest --test-dir build -L integration --output-on-failure --timeout 20
```

MongoDB connection and server-selection timeouts are bounded so a missing database
fails quickly. gRPC calls also use finite deadlines. The current integration suite
contains 14 tests.

## Entire Suite

```bash
ctest --test-dir build --output-on-failure --timeout 20
```

The complete suite currently contains 43 tests.

## Docker Smoke Test

With Docker running, validate the production-like local topology and the full HTTP
CRUD flow:

```bash
./scripts/smoke_test.sh
```

See [Local Docker Deployment](DEPLOYMENT.md) for lifecycle and troubleshooting
commands.

## Current Test Groups

- Cache unit tests: LRU, LFU, and random replacement behavior.
- HTTP unit tests: JSON handling, validation, error mapping, pagination, update, and
  readiness behavior.
- gRPC service unit tests: repository error mapping and complete post responses.
- Repository integration tests: MongoDB CRUD, aggregation, validation, and idempotent
  updates.
- gRPC integration tests: end-to-end gRPC and MongoDB behavior.

## Adding Tests

- Add tests that need no external service to `unit_tests` or `grpc_unit_tests` in
  `tests/CMakeLists.txt`.
- Add tests that require MongoDB or another external component to
  `integration_tests`.
- Every network client must use a finite deadline or timeout.
- Prefer an ephemeral local port for in-process server tests.
- Include regression coverage before fixing a reported defect whenever practical.
