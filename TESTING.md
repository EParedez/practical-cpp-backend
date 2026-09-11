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
test store. The current unit suite contains 51 tests.

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
contains 22 tests.

## Entire Suite

```bash
ctest --test-dir build --output-on-failure --timeout 20
```

The complete suite currently contains 73 tests.

## Sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer can be enabled in a separate build:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-sanitize -j4
ctest --test-dir build-sanitize -L unit --output-on-failure
```

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
- Production post-cache tests: typed values, LRU eviction, disabled capacity, TTL,
  invalidation, and concurrent access.
- Security unit tests: role enforcement, credential failures, PBKDF2, and rate limits.
- HTTP unit tests: JSON handling, authorization, security headers, CORS, validation,
  error mapping, pagination, update, and readiness behavior.
- gRPC service unit tests: repository error mapping and complete post responses.
- Repository integration tests: MongoDB CRUD, aggregation, validation, and idempotent
  updates, schema migration, native dates, indexes, filtering, and pagination.
- gRPC integration tests: end-to-end gRPC, authentication, and MongoDB behavior.

## Adding Tests

- Add tests that need no external service to `unit_tests` or `grpc_unit_tests` in
  `tests/CMakeLists.txt`.
- Add tests that require MongoDB or another external component to
  `integration_tests`.
- Every network client must use a finite deadline or timeout.
- Prefer an ephemeral local port for in-process server tests.
- Include regression coverage before fixing a reported defect whenever practical.

## Troubleshooting

- If CMake cannot find gRPC, MongoDB C++ driver, or GoogleTest on Homebrew, pass the
  `CMAKE_PREFIX_PATH` shown in the root README and ensure the build architecture
  matches the installed packages.
- If integration tests fail immediately, run `./scripts/start_mongo.sh`, then verify
  `mongosh --quiet --eval 'db.adminCommand({ping: 1})'` succeeds.
- If a previous test left port `27017` occupied, inspect the existing `mongod` process
  before starting another instance; do not delete its data directory blindly.
- If the Docker smoke test fails, rerun it with `KEEP_SMOKE_STACK=1` and inspect
  `docker compose --project-name practicalcppbackend-smoke logs`.
- If HTTP socket tests cannot bind, permit loopback networking for the test process;
  the tests do not require a public interface.
- Use a fresh build directory after changing compiler architecture, sanitizer mode, or
  dependency prefixes to avoid stale CMake cache entries.
